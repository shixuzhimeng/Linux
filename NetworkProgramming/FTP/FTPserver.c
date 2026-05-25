#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <libgen.h>

#define ROOT_PATH "./FTP"
#define PORT 2100
#define THREAD_POOL_SIZE 8
#define MAX_EVENTS 64
#define BUFFER_SIZE 8192

char g_path[1024];
int efd;                 
int g_shutdown = 0;

//client 会话
typedef struct ftp_session {
    int ctrl_fd;                // 控制连接套接字
    int data_listen_fd;         // 被动模式监听套接字
    int active_mode;            // 主动模式
    struct sockaddr_in data_addr; // 主动模式下客户端指定的数据地址
    char recv_buf[BUFFER_SIZE];
    int recv_len;
} ftp_session_t;


//任务节点
typedef struct task {
    ftp_session_t *session;
    char cmd_line[256];
    struct task *next;
} task_t;

task_t *g_task_head = NULL;
task_t *g_task_tail = NULL;
pthread_mutex_t g_task_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_task_cond = PTHREAD_COND_INITIALIZER;

int init_listenfd(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        exit(1);
    }
    
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        exit(1);
    }
    
    if(listen(fd, 128) < 0){
        exit(1);
    }
    
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    return fd;
}

void send_response(int fd, const char *code, const char *msg) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s %s\r\n", code, msg);
    send(fd, buf, strlen(buf), MSG_NOSIGNAL);
}

task_t *get_task(void) {
    pthread_mutex_lock(&g_task_mutex);
    while (g_task_head == NULL && !g_shutdown)
        pthread_cond_wait(&g_task_cond, &g_task_mutex);
    if(g_shutdown && g_task_head == NULL) {
        pthread_mutex_unlock(&g_task_mutex);
        return NULL;
    }
    task_t *t = g_task_head;
    g_task_head = g_task_head->next;
    if(g_task_head == NULL) {
        g_task_tail = NULL;
    }
    pthread_mutex_unlock(&g_task_mutex);
    return t;
}

int make_data_socket(int *listen_fd, int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        return -1;
    }
    
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    
    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr*)&addr, &len);
    *port = ntohs(addr.sin_port);
    
    if(listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    *listen_fd = fd;
    return 0;
}

void close_data_socket(int listen_fd, int data_fd) {
    if(data_fd != -1) {
        close(data_fd);
    }
    if(listen_fd != -1) {
        close(listen_fd);
    }
}

int safe_path(const char *user_path, char *out_path, size_t out_size) {
    if(!user_path || user_path[0] == '\0') {
        return -1;
    }
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s/%s", g_path, user_path);

    // 先尝试解析完整路径（适用于已存在的文件/目录）
    char resolved[2048];
    if(realpath(tmp, resolved) != NULL) {
        // 确保解析后的路径以根目录开头
        size_t root_len = strlen(g_path);
        if(strncmp(resolved, g_path, root_len) != 0) {
            return -1;
        }
        // 防止 /root_extra 这种前缀匹配错误
        if(resolved[root_len] != '\0' && resolved[root_len] != '/') {
            return -1;
        }    
        strncpy(out_path, resolved, out_size - 1);
        out_path[out_size - 1] = '\0';
        return 0;
    }

    // 文件不存在（如 STOR 新建文件），检查父目录
    char parent[2048];
    strncpy(parent, tmp, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char *last_slash = strrchr(parent, '/');
    if(!last_slash || last_slash == parent) {
        return -1;
    }
    *last_slash = '\0';

    char parent_res[2048];
    if(realpath(parent, parent_res) == NULL) {
        return -1;
    }
    size_t root_len = strlen(g_path);
    if(strncmp(parent_res, g_path, root_len) != 0) {
        return -1;
    }
    if(parent_res[root_len] != '\0' && parent_res[root_len] != '/') {
        return -1;
    }
    // 父目录合法，拼接原始文件名作为输出路径
    strncpy(out_path, tmp, out_size - 1);
    out_path[out_size - 1] = '\0';
    return 0;
}

void list_directory(int data_fd) {
    DIR *dir = opendir(g_path);
    if(!dir) {
        return ;
    }
    struct dirent *entry;
    char listing[BUFFER_SIZE] = {0};
    while ((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') {
            continue;
        }
        if(strlen(listing) + strlen(entry->d_name) + 4 > BUFFER_SIZE - 256) {
            send(data_fd, listing, strlen(listing), MSG_NOSIGNAL);
            listing[0] = '\0';
        }
        strcat(listing, entry->d_name);
        strcat(listing, "\r\n");
    }
    if(strlen(listing) > 0) send(data_fd, listing, strlen(listing), MSG_NOSIGNAL);
    closedir(dir);
}

int retr_file(const char *filename, int data_fd) {
    char full[2048];

    if(safe_path(filename, full, sizeof(full)) != 0) {
        return -1;
    }

    int fd = open(full, O_RDONLY);
    if(fd < 0) return -1;  // 文件不存在或无读权限

    struct stat st;
    if(fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }

    off_t offset = 0;
    while(offset < st.st_size) {
        ssize_t sent = sendfile(data_fd, fd, &offset, st.st_size - offset);
        if(sent <= 0) {
            break;
        }
    }
    close(fd);
    return 0;
}

//上传文件
int store_file(const char *filename, int data_fd) {
    char full[2048];
    if(safe_path(filename, full, sizeof(full)) != 0) {
        return -1;
    }
    // 确保父目录存在
    char dir_copy[2048];
    strncpy(dir_copy, full, sizeof(dir_copy) - 1);
    dir_copy[sizeof(dir_copy) - 1] = '\0';
    char *p = strrchr(dir_copy, '/');
    if(p) {
        *p = '\0';
        mkdir(dir_copy, 0755);
    }

    int fd = open(full, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) {
        perror("store_file open");
        return -1;
    }

    char buf[BUFFER_SIZE];
    ssize_t n;
    int write_ok = 1;
    while((n = recv(data_fd, buf, BUFFER_SIZE, 0)) > 0) {
        ssize_t written = 0;
        while(written < n) {
            ssize_t w = write(fd, buf + written, n - written);
            if (w <= 0) { write_ok = 0; break; }
            written += w;
        }
        if(!write_ok) {
            break;
        }
    }
    close(fd);
    return write_ok ? 0 : -1;
}

void handle_command(ftp_session_t *s, const char *cmd_line) {
    char cmd[10], arg[256];
    cmd[0] = arg[0] = '\0';
    sscanf(cmd_line, "%9s %255s", cmd, arg);
    for(char *c = cmd; *c; c++) {
        *c = toupper(*c);
    }
    printf("Thread %lu: %s\n", (unsigned long)pthread_self(), cmd_line);

    if(strcmp(cmd, "USER") == 0 || strcmp(cmd, "PASS") == 0) {
        send_response(s->ctrl_fd, "230", "Login successful");
    }
    else if(strcmp(cmd, "QUIT") == 0) {
        send_response(s->ctrl_fd, "221", "Goodbye");
        close(s->ctrl_fd);
        s->ctrl_fd = -1;
    }
    else if(strcmp(cmd, "PORT") == 0) {
        int h1,h2,h3,h4,p1,p2;
        if(sscanf(arg, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2) != 6) {
            send_response(s->ctrl_fd, "501", "Syntax error");
            return;
        }

        if(s->data_listen_fd != -1) { 
            close(s->data_listen_fd);
        }
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons((p1<<8)|p2);
        
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", h1,h2,h3,h4);
        inet_pton(AF_INET, ip_str, &addr.sin_addr);
        s->active_mode = 1;
        s->data_addr = addr;
        send_response(s->ctrl_fd, "200", "PORT command successful");
    }
    else if(strcmp(cmd, "PASV") == 0) {
        s->active_mode = 0;
        if(s->data_listen_fd != -1)
            close(s->data_listen_fd);
        int fd, port;
        if(make_data_socket(&fd, &port) != 0) {
            send_response(s->ctrl_fd, "425", "Cannot open passive connection");
            return;
        }
        s->data_listen_fd = fd;
        struct sockaddr_in local;
        socklen_t len = sizeof(local);
        getsockname(s->ctrl_fd, (struct sockaddr*)&local, &len);
        unsigned char *ip = (unsigned char*)&local.sin_addr.s_addr;
        char resp[256];
        sprintf(resp, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
                ip[0], ip[1], ip[2], ip[3], port/256, port%256);
        send_response(s->ctrl_fd, "227", resp);
    }
    else if(strcmp(cmd, "LIST") == 0 ||
            strcmp(cmd, "RETR") == 0 ||
            strcmp(cmd, "STOR") == 0) {
        int data_fd = -1;
        if(s->active_mode) {
            data_fd = socket(AF_INET, SOCK_STREAM, 0);
            if(data_fd < 0 || connect(data_fd, (struct sockaddr*)&s->data_addr, sizeof(s->data_addr)) < 0) {
                send_response(s->ctrl_fd, "425", "Cannot connect to client");
                if(data_fd >= 0) 
                    close(data_fd);
                s->active_mode = 0;
                return;
            }
            s->active_mode = 0;
        }
        else if(s->data_listen_fd != -1) {
            data_fd = accept(s->data_listen_fd, NULL, NULL);
            if(data_fd < 0) {
                send_response(s->ctrl_fd, "425", "Data connection failed");
                close_data_socket(s->data_listen_fd, -1);
                s->data_listen_fd = -1;
                return;
            }
            close_data_socket(s->data_listen_fd, -1);
            s->data_listen_fd = -1;
        }
        else{
            send_response(s->ctrl_fd, "425", "Use PORT or PASV first");
            return;
        }

        if(strcmp(cmd, "LIST") == 0) {
            send_response(s->ctrl_fd, "150", "Here comes the directory listing");
            list_directory(data_fd);
            send_response(s->ctrl_fd, "226", "Directory send OK");
        }
        // RETR直接用 retrieve_file 返回值判断
        else if(strcmp(cmd, "RETR") == 0) {
            send_response(s->ctrl_fd, "150", "Opening data connection");
            if (retr_file(arg, data_fd) == 0) {
                send_response(s->ctrl_fd, "226", "Transfer complete");
            }
            else{
                send_response(s->ctrl_fd, "550", "File not found or access denied");
            }
        }
        // STOR根据 store_file 返回值决定回复 226 还是 550
        else if(strcmp(cmd, "STOR") == 0) {
            send_response(s->ctrl_fd, "150", "Ready to receive data");
            if(store_file(arg, data_fd) == 0) {
                send_response(s->ctrl_fd, "226", "Transfer complete");
            }
            else {
                send_response(s->ctrl_fd, "550", "Failed to store file");
            }
        }
        close(data_fd);
    }
    else {
        send_response(s->ctrl_fd, "502", "Command not implemented");
    }
}

// 线程工作函数
void *worker_thread(void *arg) {
    (void)arg;
    while (!g_shutdown) {
        task_t *t = get_task();
        if(t) {
            handle_command(t->session, t->cmd_line);
            free(t);
        }
    }
    return NULL;
}

void accept_new_connection(int listen_fd) {
    struct sockaddr_in clie_addr;
    socklen_t len = sizeof(clie_addr);
    
    int ctrl_fd = accept(listen_fd, (struct sockaddr*)&clie_addr, &len);
    if(ctrl_fd < 0) {
        return;
    }
    
    int flags = fcntl(ctrl_fd, F_GETFL, 0);
    if(flags != -1) { 
        fcntl(ctrl_fd, F_SETFL, flags | O_NONBLOCK);
    }
    ftp_session_t *s = calloc(1, sizeof(ftp_session_t));
    s->ctrl_fd = ctrl_fd;
    s->data_listen_fd = -1;
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = s;
    epoll_ctl(efd, EPOLL_CTL_ADD, ctrl_fd, &ev);
    
    send_response(ctrl_fd, "220", "FTP Server ready");
    printf("New connection from %s:%d\n", inet_ntoa(clie_addr.sin_addr), ntohs(clie_addr.sin_port));
}

void add_task(ftp_session_t *s, const char *cmd) {
    task_t *t = malloc(sizeof(task_t));
    t->session = s;
    strncpy(t->cmd_line, cmd, sizeof(t->cmd_line)-1);
    t->cmd_line[sizeof(t->cmd_line)-1] = '\0';
    t->next = NULL;
    pthread_mutex_lock(&g_task_mutex);
    
    if(g_task_tail) {
        g_task_tail->next = t;
        g_task_tail = t;
    }
    else {
        g_task_head = g_task_tail = t;
    }
    
    pthread_cond_signal(&g_task_cond);
    pthread_mutex_unlock(&g_task_mutex);
}

void handle_client_read(ftp_session_t *s) {
    int fd = s->ctrl_fd;
    char buf[BUFFER_SIZE];
    while (1) {
        int n = recv(fd, buf, sizeof(buf), 0);
        if(n <= 0) {
            if(n < 0 && errno == EAGAIN) {
                break;
            }
            printf("Client disconnected\n");

            epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            
            if(s->data_listen_fd != -1)
                close(s->data_listen_fd);
            free(s);
            return;
        }
        
        // 处理数据
        if(s->recv_len + n < BUFFER_SIZE) {
            memcpy(s->recv_buf + s->recv_len, buf, n);
            s->recv_len += n;
        }
        else {
            s->recv_len = 0;
            continue;
        }

        //解析命令
        char *line = s->recv_buf;
        char *crlf;
        while((crlf = strstr(line, "\r\n")) != NULL) {
            *crlf = '\0';
            add_task(s, line);
            line = crlf + 2;
            s->recv_len -= (int)(line - s->recv_buf);
            memmove(s->recv_buf, line, s->recv_len);
            line = s->recv_buf;
        }
    }
}

int main(int argc, char *argv[]) {
    //处理路径，避免路径错乱，没有办法正确识别文件的正确路径
    if(argc > 1) {
        char *abs = realpath(argv[1], NULL);
        if(!abs) {
            perror("Invalid root path");
            return 1;
        }
        strcpy(g_path, abs);
        free(abs);
    }
    else {
        char cwd[1024];
        if(!getcwd(cwd, sizeof(cwd))) {
            perror("getcwd");
            return 1;
        }
        // 使用更大的临时缓冲区避免截断警告
        char tmp_root[2048];
        int written = snprintf(tmp_root, sizeof(tmp_root), "%s/%s", cwd, ROOT_PATH);
        if(written < 0 || (size_t)written >= sizeof(tmp_root)) {
            fprintf(stderr, "Root path too long: %s/%s\n", cwd, ROOT_PATH);
            return 1;
        }
        mkdir(tmp_root, 0755);
        char *abs = realpath(tmp_root, NULL);
        if(!abs) {
            perror("realpath root");
            return 1;
        }
        strncpy(g_path, abs, sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
        free(abs);
    }
    printf("Root: %s\n", g_path);

    int listen_fd = init_listenfd();
    
    
    efd = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    epoll_ctl(efd, EPOLL_CTL_ADD, listen_fd, &ev);

    pthread_t workers[THREAD_POOL_SIZE];
    for(int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }
    printf("FTP server running on port %d\n", PORT);
    
    
    struct epoll_event events[MAX_EVENTS];
    while (!g_shutdown) {
        int nfds = epoll_wait(efd, events, MAX_EVENTS, -1);
        for(int i = 0; i < nfds; i++) {
            if(events[i].data.fd == listen_fd) {
                accept_new_connection(listen_fd);
            }
            else {
                ftp_session_t *s = (ftp_session_t*)events[i].data.ptr;
                if(events[i].events & EPOLLIN)
                    handle_client_read(s);
                if(s->ctrl_fd == -1) {
                    epoll_ctl(efd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    free(s);
                }
            }
        }
    }
    
    
    
    g_shutdown = 1;
    pthread_cond_broadcast(&g_task_cond);
    for(int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(workers[i], NULL);
    }
    close(efd);
    close(listen_fd);
    return 0;
}
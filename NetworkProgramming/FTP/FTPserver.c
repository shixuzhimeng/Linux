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

#define ROOT_PATH "./FTP"
#define PORT 2100
#define THREAD_POOL_SIZE 10
#define MAX_EVENTSIZE 1024
#define BUFFER_SIZE 8192

char g_path[1024];  // 根目录
int efd;       // epoll
int g_shutdown = 0;

// client 会话
typedef struct ftp_session {
    int ctrl_fd;                // 控制连接套接字
    int data_listen_fd;         // 被动模式监听套接字
    int active_mode;            // 主动模式
    struct sockaddr_in data_addr; // 主动模式下客户端指定的数据地址
    char recv_buf[BUFFER_SIZE];
    int recv_len;
} ftp_session_t;

// 任务节点
typedef struct task {
    struct ftp_session *session;
    char cmd_line[256];
    struct task *next;
} task_t;

task_t *g_task_head = NULL;
task_t *g_task_tail = NULL;
pthread_mutex_t g_task_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_task_cond = PTHREAD_COND_INITIALIZER;

// 返回监听套接字
int init_listenfd(void) {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd < 0) {
        perror("socket failed\n");
        exit(1);
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed\n");
        exit(1);
    }

    if(listen(listenfd, 128) < 0) {
        perror("listen falied\n");
        exit(1);
    }

    int flag = 1;
    fcntl(listenfd, F_SETFL, flag | O_NONBLOCK);
    return listenfd;
}

void send_response(int fd, const char *code, const char *msg) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s %s\r\n", code, msg);
    send(fd, buffer, strlen(buffer), 0);
}

// 获取任务
task_t *get_task(void) {
    pthread_mutex_lock(&g_task_mutex);
    // 没有任务，阻塞等待
    while (g_task_head == NULL && !g_shutdown) {
        pthread_cond_wait(&g_task_cond, &g_task_mutex);
    }
    // 关闭了且任务队列为空
    if (g_shutdown && g_task_head == NULL) {
        pthread_mutex_unlock(&g_task_mutex);
        return NULL;
    }
    task_t *task = g_task_head;
    g_task_head = g_task_head->next;
    if (g_task_head == NULL) g_task_tail = NULL;
    pthread_mutex_unlock(&g_task_mutex);
    return task;
}


int make_socket(int *listenfd, int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr*)&addr, &len);
    *port = ntohs(addr.sin_port);
    if (listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    *listenfd = fd;
    return 0;
}
void close_data_socket(int listen_fd, int data_fd) {
    if (data_fd != -1)
        close(data_fd);
    if (listen_fd != -1)
        close(listen_fd);
}

// 列出目录
void list_directory(int data_fd) {
    DIR *dir = opendir(g_path);
    if(!dir) {
        return ;
    }

    struct dirent *entry;
    char listing[BUFFER_SIZE] = {0};

    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') {
            continue;
        }
        strcat(listing, entry->d_name);
        strcat(listing, "\r\n");
        if(strlen(listing) > BUFFER_SIZE - 256) {
            send(data_fd, listing, strlen(listing), 0);
            listing[0] = '\0';
        }
    }
    if(strlen(listing) > 0) {
        send(data_fd, listing, strlen(listing), 0);
    }
    closedir(dir);
}
// 保证路径安全
int safe_path(const char *user_path, char *out_path) {
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s/%s", g_path, user_path);
    char resolved[2048];
    if (realpath(tmp, resolved) == NULL) return -1;
    if (strncmp(resolved, g_path, strlen(g_path)) != 0) return -1;
    strcpy(out_path, resolved);
    return 0;
}

// 下载
void retr_file(const char *filename, int data_fd) {
    char path[512];
    if(safe_path(filename, path) != 0) {
        return ;
    }
    int file_fd = open(path, O_RDONLY);
    if(file_fd < 0) {
        return ;
    }
    struct stat st;
    fstat(file_fd, &st);
    off_t offset = 0;
    while (offset < st.st_size) {
        ssize_t sent = sendfile(data_fd, file_fd, &offset, st.st_size - offset);
        if (sent <= 0) break;
    }
    close(file_fd);
}

// 上传
void stor_file(const char *filename, int data_fd){
    char full_path[512];
    if (safe_path(filename, full_path) != 0) {
        return;
    }
    int file_fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        return;
    }
    char buffer[BUFFER_SIZE];
    ssize_t n;
    while ((n = recv(data_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        if (write(file_fd, buffer, n) != n) break;
    }
    close(file_fd);
}

// 处理命令
void handle_command(ftp_session_t *session, const char *cmd_line) {
    char cmd[10], arg[256];
    cmd[0] = arg[0] = '\0';
    sscanf(cmd_line, "%s %255s", cmd, arg);
    for (char *c = cmd; *c; c++) *c = toupper(*c);

    printf("Thread %lu handling: %s\n", pthread_self(), cmd_line);

    if (strcmp(cmd, "USER") == 0 || strcmp(cmd, "PASS") == 0) {
        send_response(session->ctrl_fd, "230", "Login successful");
    }
    else if (strcmp(cmd, "QUIT") == 0) {
        send_response(session->ctrl_fd, "221", "Goodbye");
        close(session->ctrl_fd);
        session->ctrl_fd = -1;
    }
    else if (strcmp(cmd, "PORT") == 0) {
        // 格式: h1,h2,h3,h4,p1,p2
        int h1, h2, h3, h4, p1, p2;
        if (sscanf(arg, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
            send_response(session->ctrl_fd, "501", "Syntax error in parameters");
            return;
        }
        if (h1 < 0 || h1 > 255 ||
            h2 < 0 || h2 > 255 || 
            h3 < 0 || h3 > 255 || 
            h4 < 0 || h4 > 255 ||
            p1 < 0 || p1 > 255 || 
            p2 < 0 || p2 > 255) {
            send_response(session->ctrl_fd, "501", "Invalid address/port");
            return;
        }

        // 如果之前有被动模式监听套接字，关闭它
        if (session->data_listen_fd != -1) {
            close(session->data_listen_fd);
            session->data_listen_fd = -1;
        }

        // 构造客户端数据地址（修复字节序错误）
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((p1 << 8) | p2);
        // 使用 inet_pton 正确转换点分十进制 IP 到网络字节序
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", h1, h2, h3, h4);
        if (inet_pton(AF_INET, ip_str, &addr.sin_addr) != 1) {
            send_response(session->ctrl_fd, "501", "Invalid IP address");
            return;
        }

        session->active_mode = 1;
        session->data_addr = addr;

        send_response(session->ctrl_fd, "200", "PORT command successful");
    }
    else if (strcmp(cmd, "PASV") == 0) {
        // 切换到被动模式，清除主动模式标志
        session->active_mode = 0;
        if (session->data_listen_fd != -1)
            close(session->data_listen_fd);
        int listen_fd, port;
        if (make_socket(&listen_fd, &port) != 0) {
            send_response(session->ctrl_fd, "425", "Cannot open passive connection");
            return;
        }
        session->data_listen_fd = listen_fd;
        struct sockaddr_in local_addr;
        socklen_t addr_len = sizeof(local_addr);
        getsockname(session->ctrl_fd, (struct sockaddr*)&local_addr, &addr_len);
        unsigned char *ip = (unsigned char*)&local_addr.sin_addr.s_addr;
        unsigned char p1 = (port >> 8) & 0xFF;
        unsigned char p2 = port & 0xFF;
        char response[256];
        sprintf(response, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
                ip[0], ip[1], ip[2], ip[3], p1, p2);
        send_response(session->ctrl_fd, "227", response);
    }
    else if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "RETR") == 0 || strcmp(cmd, "STOR") == 0) {
        int data_fd = -1;

        // 根据模式建立数据连接
        if (session->active_mode) {
            // 主动模式：连接客户端提供的地址和端口
            data_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (data_fd < 0) {
                send_response(session->ctrl_fd, "425", "Cannot create socket");
                return;
            }

            if (connect(data_fd, (struct sockaddr*)&session->data_addr, sizeof(session->data_addr)) < 0) {
                send_response(session->ctrl_fd, "425", "Cannot connect to client port");
                close(data_fd);
                session->active_mode = 0;  // 清除主动模式标志
                return;
            }
            // PORT 只对一次数据传输有效，使用后立即清除
            session->active_mode = 0;
        }
        else if (session->data_listen_fd != -1) {
            // 被动模式：接受客户端连接
            data_fd = accept(session->data_listen_fd, NULL, NULL);
            if (data_fd < 0) {
                send_response(session->ctrl_fd, "425", "Data connection failed");
                close_data_socket(session->data_listen_fd, -1);
                session->data_listen_fd = -1;
                return;
            }
            // 被动模式监听套接字只使用一次，立即关闭
            close_data_socket(session->data_listen_fd, -1);
            session->data_listen_fd = -1;
        }
        else {
            send_response(session->ctrl_fd, "425", "Use PORT or PASV first");
            return;
        }

        // 执行具体数据传输
        if (strcmp(cmd, "LIST") == 0) {
            send_response(session->ctrl_fd, "150", "Here comes the directory listing");
            list_directory(data_fd);
            send_response(session->ctrl_fd, "226", "Directory send OK");
        } 
        else if (strcmp(cmd, "RETR") == 0) {
            send_response(session->ctrl_fd, "150", "Opening data connection");
            // 检查文件是否存在并可读
            char full_path[512];
            if (safe_path(arg, full_path) != 0) {
                send_response(session->ctrl_fd, "550", "File not found or access denied");
            } 
            else {
                int file_fd = open(full_path, O_RDONLY);
                if (file_fd < 0) {
                    send_response(session->ctrl_fd, "550", "Cannot open file");
                } 
                else {
                    close(file_fd);
                    retr_file(arg, data_fd);
                    send_response(session->ctrl_fd, "226", "Transfer complete");
                }
            }
        } 
        else if (strcmp(cmd, "STOR") == 0) {
            send_response(session->ctrl_fd, "150", "Ready to receive data");
            char full_path[512];
            if (safe_path(arg, full_path) != 0) {
                char tmp_path[2048];
                snprintf(tmp_path, sizeof(tmp_path), "%s/%s", g_path, arg);
                char *last_slash = strrchr(tmp_path, '/');
                if (last_slash) *last_slash = '\0';
                if (access(tmp_path, W_OK) != 0) {
                    send_response(session->ctrl_fd, "553", "Cannot write to directory");
                } 
                else {
                    stor_file(arg, data_fd);
                    send_response(session->ctrl_fd, "226", "Transfer complete");
                }
            } 
            else {
                // 文件已存在，检查是否可写
                if (access(full_path, W_OK) != 0) {
                    send_response(session->ctrl_fd, "553", "Permission denied");
                } else {
                    stor_file(arg, data_fd);
                    send_response(session->ctrl_fd, "226", "Transfer complete");
                }
            }
        }

        // 关闭数据连接
        close(data_fd);
    }
    else {
        send_response(session->ctrl_fd, "502", "Command not implemented");
    }
}

void *work_thread() {
    while (!g_shutdown) {
        task_t *task = get_task();
        if (task == NULL) continue;
        handle_command(task->session, task->cmd_line);
        free(task);
    }
    return NULL;
}

void acceptconnect(int listenfd) {
    struct sockaddr_in clie_addr;
    socklen_t len = sizeof(clie_addr);

    int ctrl_fd = accept(listenfd, (struct sockaddr *)&clie_addr, &len);
    if(ctrl_fd < 0) {
        perror("accept failed\n");
        return ;
    }

    // 设置 ctrl_fd 为非阻塞
    int flag = 1;
    fcntl(ctrl_fd, F_SETFL, flag | O_NONBLOCK);

    ftp_session_t *session = (ftp_session_t *)calloc(1, sizeof(ftp_session_t));
    session->ctrl_fd = ctrl_fd;
    session->data_listen_fd = -1;
    session->recv_len = 0;

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = session;
    if(epoll_ctl(efd, EPOLL_CTL_ADD, ctrl_fd, &event) == -1) {
        perror("epoll_ctl failed\n");
        close(ctrl_fd);
        free(session);
        return ;
    }
    send_response(ctrl_fd, "220", "server ready");
    printf("New connect from %s:%d\n", 
        inet_ntoa(clie_addr.sin_addr),
        ntohs(clie_addr.sin_port));
}
// 将每个命令添加到任务队列中
void add_task(ftp_session_t *session, const char *cmd_line) {
    task_t *task = (task_t*)malloc(sizeof(task_t));
    task->session = session;
    strncpy(task->cmd_line, cmd_line, sizeof(task->cmd_line)-1);
    task->cmd_line[sizeof(task->cmd_line)-1] = '\0';
    task->next = NULL;
    pthread_mutex_lock(&g_task_mutex);
    if (g_task_tail == NULL) {
        g_task_head = g_task_tail = task;
    } else {
        g_task_tail->next = task;
        g_task_tail = task;
    }
    pthread_cond_signal(&g_task_cond);
    pthread_mutex_unlock(&g_task_mutex);
}

// 客户端处理
void handle_client(ftp_session_t *session) {
    int fd = session->ctrl_fd;
    char buf[BUFFER_SIZE];
    int n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {  
        if (n < 0 && errno == EAGAIN) return;
        printf("Client disconnected\n");
        epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        if (session->data_listen_fd != -1) 
            close(session->data_listen_fd);
        free(session);
        return;
    }
    if (session->recv_len + n < BUFFER_SIZE) {
        memcpy(session->recv_buf + session->recv_len, buf, n);
        session->recv_len += n;
    } else {
        session->recv_len = 0;
        return;
    }
    char *line_start = session->recv_buf;
    char *crlf;
    while ((crlf = strstr(line_start, "\r\n")) != NULL) {
        *crlf = '\0';
        add_task(session, line_start);
        line_start = crlf + 2;
        session->recv_len -= (line_start - session->recv_buf);
        memmove(session->recv_buf, line_start, session->recv_len);
        line_start = session->recv_buf;
    }
}

int main(int argc, char *argv[]) {
    // 设置根目录
    if(argc > 1) {
        strncpy(g_path, argv[1], sizeof(g_path) - 1);
    }
    else {
        strncpy(g_path, ROOT_PATH, sizeof(ROOT_PATH) - 1);
    }
    mkdir(g_path, 0755);
    
    // 接收返回的监听套接字
    int listenfd = init_listenfd();
    
    efd = epoll_create(10);
    if(efd < 0) {
        perror("epoll_create failed\n");
    }


    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = listenfd;

    if(epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event)) {
        perror("ctl falied\n");
        exit(1);
    }

    // 线程池启动
    pthread_t works[THREAD_POOL_SIZE];
    for(int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&works[i], NULL, work_thread, NULL);
    }
    printf("FTP server start...\n");

    // 事件处理
    struct epoll_event sevents[MAX_EVENTSIZE];
    while(!g_shutdown) {
        int nfd = epoll_wait(efd, sevents, MAX_EVENTSIZE, -1);
        // 只遍历实际发生的事件数
        for(int i = 0; i < nfd; i++) {
            if(sevents[i].data.fd == listenfd) {
                acceptconnect(listenfd);
            }
            else {
                ftp_session_t *session = (ftp_session_t *)sevents[i].data.ptr;
                if(sevents[i].events & EPOLLIN) {
                    handle_client(session);
                }
                if(session->ctrl_fd == -1){
                    epoll_ctl(efd, EPOLL_CTL_DEL, sevents[i].data.fd, NULL);
                    free(session);
                }
            }
        }
    }

    close(listenfd);
    close(efd);

    g_shutdown = 1;

    pthread_cond_broadcast(&g_task_cond);
    // 从0开始回收所有线程
    for(int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(works[i], NULL);
    }
    
    return 0;
}
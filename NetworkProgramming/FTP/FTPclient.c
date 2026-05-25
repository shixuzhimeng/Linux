#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define CTRL_PORT 2100
#define BUFFER_SIZE 8192

int ctrl_fd = -1;   // 控制连接 socket

// 读取一行服务器响应
int read_response(char *resp, size_t size) {
    char buf[BUFFER_SIZE];
    int n = recv(ctrl_fd, buf, sizeof(buf) - 1, 0);
    if(n <= 0) {
        if(n == 0){
            fprintf(stderr, "Server closed control connection\n");
        }
        else{
            perror("recv");
        }
        return -1;
    }
    buf[n] = '\0';
    if(resp) {
        strncpy(resp, buf, size - 1);
        resp[size - 1] = '\0';
    }
    printf("S: %s", buf);
    return 0;
}

// 发送命令并接收响应（自动添加 \r\n）
int send_command(const char *cmd, char *resp, size_t resp_size) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s\r\n", cmd);
    if(send(ctrl_fd, buffer, strlen(buffer), 0) <= 0) {
        perror("send");
        return -1;
    }
    return read_response(resp, resp_size);
}

// 解析 PASV 响应，提取 IP 和端口
int parse_pasv(const char *resp, char *ip, int *port) {
    char *start = strchr(resp, '(');
    char *end = strchr(resp, ')');
    if(!start || !end) {
        return -1;
    }
    int h1, h2, h3, h4, p1, p2;
    if(sscanf(start, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        return -1;
    }
    sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = p1 * 256 + p2;
    return 0;
}

// 连接数据端口（被动模式）
int connect_data(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }
    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// 列出目录
void list_directory(void) {
    char resp[BUFFER_SIZE];
    if(send_command("PASV", resp, sizeof(resp)) != 0) {
        printf("Failed to enter passive mode\n");
        return;
    }
    char data_ip[16];
    int data_port;
    if(parse_pasv(resp, data_ip, &data_port) != 0) {
        printf("Failed to parse PASV response\n");
        return;
    }
    int data_fd = connect_data(data_ip, data_port);
    if(data_fd < 0) {
        printf("Data connection failed\n");
        return;
    }
    if(send_command("LIST", resp, sizeof(resp)) != 0) {
        close(data_fd);
        return;
    }
    if(strncmp(resp, "150", 3) != 0) {
        printf("LIST not accepted by server\n");
        close(data_fd);
        return;
    }
    char buf[BUFFER_SIZE];
    int n;
    printf("--- Listing ---\n");
    while ((n = recv(data_fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    close(data_fd);
    // 接收 226 完成响应
    if(read_response(resp, sizeof(resp)) != 0 || strncmp(resp, "226", 3) != 0) {
        printf("Failed to receive completion response\n");
    }
    printf("--- End ---\n");
}

// 下载文件
void download_file(const char *filename) {
    char resp[BUFFER_SIZE];
    if(send_command("PASV", resp, sizeof(resp)) != 0) {
        return;
    }
    char data_ip[16];
    int data_port;
    if(parse_pasv(resp, data_ip, &data_port) != 0) {
        printf("Failed to parse PASV response\n");
        return;
    }
    int data_fd = connect_data(data_ip, data_port);
    if(data_fd < 0) {
        printf("Data connection failed\n");
        return;
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "RETR %s", filename);
    if(send_command(cmd, resp, sizeof(resp)) != 0) {
        close(data_fd);
        return;
    }
    if(strncmp(resp, "150", 3) != 0) {
        printf("Server refused RETR\n");
        close(data_fd);
        return;
    }
    FILE *fp = fopen(filename, "wb");
    if(!fp) {
        perror("fopen");
        close(data_fd);
        return;
    }
    char buf[BUFFER_SIZE];
    int n;
    while((n = recv(data_fd, buf, sizeof(buf), 0)) > 0) {
        fwrite(buf, 1, n, fp);
    }
    fclose(fp);
    close(data_fd);
    // 接收 226 完成响应
    if(read_response(resp, sizeof(resp)) == 0 && strncmp(resp, "226", 3) == 0) {
        printf("Downloaded %s\n", filename);
    }
    else {
        printf("Download failed: unexpected response\n");
    }
}

// 上传文件
void upload_file(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if(!fp) {
        perror("fopen");
        return;
    }
    char resp[BUFFER_SIZE];
    if(send_command("PASV", resp, sizeof(resp)) != 0) {
        fclose(fp);
        return;
    }
    char data_ip[16];
    int data_port;
    if(parse_pasv(resp, data_ip, &data_port) != 0) {
        printf("Failed to parse PASV response\n");
        fclose(fp);
        return;
    }
    int data_fd = connect_data(data_ip, data_port);
    if(data_fd < 0) {
        printf("Data connection failed\n");
        fclose(fp);
        return;
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "STOR %s", filename);
    if(send_command(cmd, resp, sizeof(resp)) != 0) {
        close(data_fd);
        fclose(fp);
        return;
    }
    if(strncmp(resp, "150", 3) != 0) {
        printf("Server refused STOR\n");
        close(data_fd);
        fclose(fp);
        return;
    }
    char buf[BUFFER_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if(send(data_fd, buf, n, 0) != (ssize_t)n) {
            perror("send");
            break;
        }
    }
    fclose(fp);
    close(data_fd);
    // 接收 226 完成响应
    if(read_response(resp, sizeof(resp)) == 0 && strncmp(resp, "226", 3) == 0) {
        printf("Uploaded %s\n", filename);
    }
    else {
        printf("Upload failed: unexpected response\n");
    }
}

// 连接控制服务器
int connect_control(const char *ip) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CTRL_PORT);
    if(inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char *argv[]) {
    // 忽略 SIGPIPE，防止向已关闭 socket 发送数据时进程终止
    signal(SIGPIPE, SIG_IGN);

    char ip[64];
    if(argc > 1) {
        strcpy(ip, argv[1]);
    }
    else {
        printf("Enter server IP: ");
        fgets(ip, sizeof(ip), stdin);
        ip[strcspn(ip, "\n")] = '\0';
    }

    ctrl_fd = connect_control(ip);
    if(ctrl_fd < 0) {
        perror("connect");
        return 1;
    }
    
    // 读取欢迎消息
    if(read_response(NULL, 0) != 0) {
        close(ctrl_fd);
        return 1;
    }

    // 登录
    if(send_command("USER test", NULL, 0) != 0 ||
        send_command("PASS test", NULL, 0) != 0) {
        close(ctrl_fd);
        return 1;
    }
    printf("Connected to %s:%d\n", ip, CTRL_PORT);
    printf("Commands: ls, get <file>, put <file>, quit\n");

    char input[256];
    while (1) {
        printf("ftp> ");
        fflush(stdout);
        
        if(!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        input[strcspn(input, "\n")] = '\0';
        if(strlen(input) == 0) {
            continue;
        }
        
        if(strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            send_command("QUIT", NULL, 0);
            break;
        }
        else if (strcmp(input, "ls") == 0 || strcmp(input, "dir") == 0) {
            list_directory();
        }
        else if (strncmp(input, "get ", 4) == 0) {
            download_file(input + 4);
        }
        else if (strncmp(input, "put ", 4) == 0) {
            upload_file(input + 4);
        }
        else {
            printf("Unknown command. Try: ls, get <file>, put <file>, quit\n");
        }
    }
    
    close(ctrl_fd);
    return 0;
}

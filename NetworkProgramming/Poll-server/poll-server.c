#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include "wrap.h"
#include <poll.h>

#define SERV_PORT 9999
#define MAXLINE 80
#define OPEN_MAX 1024


int main(int argc, char *argv[]) {
    int i, j, maxi, listenfd, connfd, sockfd;
    int nready;
    ssize_t n;

    char wbuf[BUFSIZ];
    char buf[BUFSIZ];
    char str[INET_ADDRSTRLEN];
    socklen_t clielen;
    struct pollfd clients[OPEN_MAX];
    struct sockaddr_in serv_addr, clie_addr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    Bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    Listen(listenfd, 128);

    clients[0].fd = listenfd;       // 要监听的第一个文件描述符，存入clients数组
    clients[0].events = POLLIN;     // listenfd监听普通读事件 

    for(i = 1; i < OPEN_MAX; i++) {
        clients[i].fd = -1;         // 用-1初始化数组
    }
    
    maxi = 0;         // clients数组中的最大有效元素的下标

    for(;;) {
        nready = poll(clients, maxi + 1, -1);  //阻塞监听是否有用户端连接请求
        if(clients[0].revents & POLLIN) {      //listenfd有读事件就绪
            clielen = sizeof(clie_addr);
            connfd = Accept(listenfd, (struct sockaddr *)&clie_addr, &clielen);  //接受客户端Accept请求就不会阻塞了
            printf("received from %s at PORT %d\n", 
            inet_ntop(AF_INET, &clie_addr.sin_addr, str, sizeof(str)),
            ntohs(clie_addr.sin_port));        // 这里用换行来刷新缓冲区
            // inet_ntop(AF_INET, &clie_addr.sin_addr, wbuf, sizeof(wbuf));
            // Write(1, wbuf, sizeof(wbuf));
            
            for(i = 1; i < OPEN_MAX; i++) {
                if(clients[i].fd < 0) {
                    clients[i].fd = connfd;   //找到clients中空闲的位置存放返回的connfd
                    break;
                }
            }
            if(i == OPEN_MAX) {              //达到最大的客户端的数量
                perr_exit("too many client\n");
            }
            clients[i].events = POLLIN;      //设置刚刚返回的connfd，监控读事件

            if(i > maxi) {
                maxi = i;                    //更新clients中的最大元素的下标
            }
            if(--nready == 0) {
                continue;                    //没有更多就绪事件，继续返回poll阻塞等待
            }
        }
        for(i = 1; i <= maxi; i++) {         //前面的if没满足，说明没有lsitenfd满足，检测clients数组，看看是那个connfd就绪
            if((sockfd = clients[i].fd) < 0) {
                continue;                    //相当于异常处理，后续代码直接跳过
            }
            if(clients[i].revents & POLLIN) {
                if((n = Read(sockfd, buf, MAXLINE)) < 0) {
                    if(errno == ECONNRESET) {   // 收到RST标志
                        printf("clinet[%d] aborted connection\n", i);
                        close(sockfd);
                        clients[i].fd = -1;     //poll中不监听该文件描述符，直接设置为-1；不用select那样移除
                    }
                    else {
                        perr_exit("read error");
                    }
                }
                else if(n == 0){               //说明客户端现关闭链接
                    printf("clients[%d] closed connection\n", i);
                    close(sockfd);
                    clients[i].fd = -1;
                }
                else {
                    for(j = 0; j < n; j++) {
                        buf[j] = toupper(buf[j]);
                    }
                    Write(sockfd, buf, n);
                }
                if(--nready <= 0) {
                    break;
                }
            }
        }
    }
    

    
    return 0;
}
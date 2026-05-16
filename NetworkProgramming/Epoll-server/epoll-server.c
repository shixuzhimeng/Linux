#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "wrap.h"

#define SERV_PORT 9999
#define MAXLINE 8192
#define OPEN_MAX 5000

int main (int argc, char *argv[]) {
    int i, j, listenfd, connfd, sockfd;
    int n, num;
    char buf[MAXLINE], str[MAXLINE];
    socklen_t client;
    ssize_t nready, efd, res;

    struct sockaddr_in serv_addr, clie_addr;
    struct epoll_event tep, ep[OPEN_MAX];  // tep: epoll参数， ep[]： epoll_wait参数
    
    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    Bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    Listen(listenfd, 128);

    efd = epoll_create(OPEN_MAX);       //创建EPOLL模型，efd指向红黑树的根结点
    if(efd == -1) {
        perr_exit("epoll error");;
    }
    tep.events = EPOLLIN;
    tep.data.fd = listenfd;
    res = epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &tep);     // 将lfd对应的结构体设置到树上，efd可以找到该树

    if(res == -1) {
        perr_exit("epoll_ctl error");
    }
    for( ; ; ) {
        nready = epoll_wait(efd, ep, OPEN_MAX, -1);
        if(nready == -1) {
            perr_exit("epoll_wait error");
        }
        for(i = 0; i < nready; i++) {
            if(!(ep[i].events & EPOLLIN)) {
                continue;
            }
            if(ep[i].data.fd == listenfd) {
                client = sizeof(clie_addr);
                connfd = Accept(listenfd, (struct sockaddr *)&clie_addr, &client);
                printf("received from %s at PORT %d\n", 
                    inet_ntop(AF_INET, &clie_addr.sin_addr, str, sizeof(str)),
                    ntohs(clie_addr.sin_port));
                printf("cfd %d --- client %d\n",connfd, ++num);
                
                tep.events = EPOLLIN;
                tep.data.fd = connfd;
                res = epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &tep); // 加入红黑树

                if(res == -1) {
                    perr_exit("epoll_ctl error");
                }
            }
            else {
                sockfd = ep[i].data.fd;
                n = Read(sockfd, buf, MAXLINE);
                
                if(n == 0) {
                    res = epoll_ctl(efd, EPOLL_CTL_DEL, sockfd, NULL);
                    if(res == -1) {
                        perr_exit("epoll_ctl error");
                    }
                    close(sockfd);
                    printf("client[%d] closed connection\n", sockfd); // 关闭链接
                }
                else if(n < 0) {
                    perror("read n < 0 error");
                    res = epoll_ctl(efd, EPOLL_CTL_DEL, sockfd, NULL);  //出错， 摘除节点
                    close(sockfd);
                }
                else {
                    for(i = 0; i < n; i++) {
                        buf[i] = toupper(buf[i]);
                    }
                    Write(STDOUT_FILENO, buf, n);
                    Write(sockfd, buf, n);
                }
            }
        }
    }
    close(listenfd);
    close(efd);
    return 0;
}
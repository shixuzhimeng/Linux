#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>


#define SERV_PORT 9999
#define MAXLINE 10

int main (void) {
    struct sockaddr_in serv_addr, clie_addr;
    socklen_t clientlen;
    int listenfd, connfd;
    char buf[MAXLINE];
    char str[INET_ADDRSTRLEN];
    int efd, flag;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    listen(listenfd, 128);

    struct epoll_event event;
    struct epoll_event res_event[10];
    int res, len;

    efd = epoll_create(10);

    event.events = EPOLLIN | EPOLLET;

    printf("Accept connection ...\n");
    clientlen = sizeof(clie_addr);
    connfd = accept(listenfd, (struct sockaddr *)&clie_addr, &clientlen);
    printf("received from %s at PORT%d\n", 
        inet_ntop(AF_INET, &clie_addr.sin_addr, buf, sizeof(buf)), 
        ntohs(clie_addr.sin_port));
    
    //获取connfd状态，然后将其设置为非阻塞状态
    flag = fcntl(connfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(connfd, F_SETFL, flag);

    event.data.fd = connfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event);
    while(1) {
        printf("epoll_wait begin\n");
        res = epoll_wait(efd, res_event, 10, -1);
        printf("epoll_wait end res %d\n",res);

        if(res_event[0].data.fd == connfd) {
            //非阻塞读 轮询
            while((len = read(connfd, buf, MAXLINE/2)) > 0) {
                write(STDOUT_FILENO, buf, len);
            }
        }
    }

    return 0;
}
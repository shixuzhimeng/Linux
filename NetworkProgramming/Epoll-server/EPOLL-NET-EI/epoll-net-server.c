#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERV_PORT 9999
#define MAXLINE 10

int main(void) {
    struct sockaddr_in serv_addr, clie_addr;
    socklen_t clientlen;
    int listenfd, connfd;
    char buf[MAXLINE];
    char str[INET_ADDRSTRLEN];
    int efd;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&serv_addr ,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    listen(listenfd, 128);

    struct epoll_event event;
    struct epoll_event resevent[10];

    int res, len;
    efd = epoll_create(10);

    event.events = EPOLLIN | EPOLLET;
    //event.events = EPOLLIN;

    printf("Accepting connecton ...\n");

    clientlen = sizeof(clie_addr);
    connfd = accept(listenfd, (struct sockaddr *)&clie_addr, &clientlen);
    printf("received from %s at PORT %d\n", 
            inet_ntop(AF_INET, &clie_addr.sin_addr, str, sizeof(str)),
            ntohs(clie_addr.sin_port));
    event.data.fd = connfd;
    epoll_ctl(efd,EPOLL_CTL_ADD, connfd, &event);
    while(1) {
        res = epoll_wait(efd, resevent, 10, -1);
        printf("res %d", res);
        if(resevent[0].data.fd = connfd) {
            len = read(connfd, buf, sizeof(buf));
        
            write(STDOUT_FILENO, buf, sizeof(buf));
        }
    }
    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "wrap.h"

#define MAXLINE 8192
#define SERV_PORT 9999


//将地址结构体和cfd捆绑
struct s_info {
    struct sockaddr_in childaddr;
    int connfd;
};

void *do_work(void *arg) {
    int n, i;
    struct s_info *ts = (struct s_info*)arg;
    char buf[MAXLINE];
    char str[INET_ADDRSTRLEN];  // #define INET_ADDRSTRLEN 16

    while(1) {
        n = Read(ts->connfd, buf, MAXLINE);        //读客户端
        //跳出循环，关闭cfd
        if(n == 0){
            printf("the client %d closed...\n", ts->connfd);
            break;
        }
        //打印客户端的信息
        printf("received from %s at PORT %d\n", 
            inet_ntop(AF_INET, &(*ts).childaddr.sin_addr, str, sizeof(str)), 
            ntohs((*ts).childaddr.sin_port));
        for(i = 0; i < n; i++) {
            buf[i] = toupper(buf[i]);
        }
        write(STDOUT_FILENO, buf, n);    // 写出屏幕
        write(ts->connfd, buf, n);       // 写回到客户端

    }
    close(ts->connfd);

    return (void*)0;
}

int main(void) {
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len;
    int listenfd, connfd;
    pthread_t tid;

    struct s_info ts[256];
    int i = 0;
    
    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    Bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    Listen(listenfd, 128);

    printf("Accept client connect ... \n");

    while(1) {
        cliaddr_len = sizeof(cliaddr);
        connfd = Accept(listenfd,(struct sockaddr *)&cliaddr, &cliaddr_len);
        ts[i].childaddr = cliaddr;
        ts[i].connfd = connfd;
        
        pthread_create(&tid, NULL, do_work, (void*)&ts[i]);
        pthread_detach(tid);
        i++;
    }

    return 0;
}
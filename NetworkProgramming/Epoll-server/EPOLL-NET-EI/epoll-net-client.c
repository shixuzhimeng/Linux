#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAXLINE 10
#define SERV_PORT 9999

int main (int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    char buf[MAXLINE];
    int sockfd, i;
    char ch = 'a';

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
    
    connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    while(1) {
        for(i = 0; i < MAXLINE / 2; i++) {
            buf[i] = ch;
        }
        buf[i - 1] = '\n';
        ch++;
        for(; i < MAXLINE; i++) {
            buf[i] = ch;
        }
        buf[i - 1] = '\n';
        ch++;
        write(sockfd, buf, sizeof(buf));
        sleep(5);
    }
    close(sockfd);
    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>

#define SERV_PORT 9999

int main() {
    struct sockaddr_in serv_addr;
    int socked, n;
    char buf[BUFSIZ];

    socked = socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
    serv_addr.sin_port = htons(SERV_PORT);

    //bind(socked, (struct sockaddr *)&serv_addr, sizeof(serv_addr));    //无效

    while(fgets(buf, BUFSIZ, stdin)) {
        n = sendto(socked, buf, strlen(buf), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        if(n == -1) {
            perror("sendto failed");
        }
        n = recvfrom(socked, buf, BUFSIZ, 0, NULL, 0);    // 不关心对端情况
        if(n == -1) {
            perror("recvfrom failed");
        }
        write(STDOUT_FILENO, buf, n);
    }
    close(socked);
    return 0;
}
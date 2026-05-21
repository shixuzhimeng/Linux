#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>

#define SERV_PORT 9999

int main() {
    int socked;
    struct sockaddr_in serv_addr, clie_addr;
    char buf[BUFSIZ];
    char str[INET_ADDRSTRLEN];
    socklen_t clientlen;

    int i, n;

    socked = socket(AF_INET, SOCK_DGRAM, 0);
    
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(socked, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    printf("Accept connect...\n");

    while(1) {
        clientlen = sizeof(clie_addr);
        n = recvfrom(socked, buf, BUFSIZ, 0, (struct sockaddr *)&clie_addr, &clientlen);
        if(n == -1) {
            perror("recvfrom failed");
        }
        printf("received from %s at PORT %d\n", 
        inet_ntop(AF_INET, &clie_addr.sin_addr, str, sizeof(str)),
        ntohs(clie_addr.sin_port));
        for(i = 0; i < n; i++) {
            buf[i] = toupper(buf[i]);
        }
        n = sendto(socked, buf, n, 0, (struct sockaddr *)&clie_addr, sizeof(clie_addr));
        if(n == -1) {
            perror("sendto failed");
        }
    }
    close(socked);

    return 0;
}
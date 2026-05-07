#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <arpa/inet.h>
//#include <netinet/in.h>
#include <pthread.h>

void sys_err(const char *str) {
    perror(str);
    exit(1);
}

int main(int argc, char *argv[]) {
    int cfd = 0;
    struct sockaddr_in serv_addr;
    
    cfd = socket(AF_INET, SOCK_STREAM, 0);
    if(cfd == -1) {
        sys_err("socket error");
    }

    connect();

    return 0;
}
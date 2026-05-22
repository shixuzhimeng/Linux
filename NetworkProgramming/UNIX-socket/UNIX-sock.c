#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <stddef.h>
#include <sys/un.h>

#define SERV_ADDR "serv.socket"

int main(void) {
    int lfd, cfd;
    int len, size, i;
    struct sockaddr_un servaddr, clieaddr;
    char buf[4096];

    lfd = socket(AF_UNIX, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, SERV_ADDR);
    
    len = offsetof(struct sockaddr_un, sun_path) + strlen(servaddr.sun_path);
    
    unlink(SERV_ADDR);              // 确保bind之前没有serv.sock文件名不存在， bind会创建这个文件（伪文件）
    bind(lfd, (struct sockaddr *)&servaddr, len);  // 参数3，不能设置为sizeof(servaddr)!

    listen(lfd, 128);

    printf("Accept ...\n");

    while(1) {
        len = sizeof(clieaddr);
        
        cfd = accept(lfd, (struct sockaddr *)&clieaddr, len);

        len -= offsetof(struct sockaddr_un, sun_path);  // 得到文件名长度
        clieaddr.sun_path[len] = '\0';

        printf("client bind filename %s\n", clieaddr.sun_path);
        
        while((size = read(cfd, buf, sizeof(buf))) > 0) {
            for(i = 0; i < size; i++) {
                buf[i] = toupper(buf[i]);
            }
            write(cfd, buf, size);
        }
        close(cfd);
    }
    close(lfd);
    return 0;
}
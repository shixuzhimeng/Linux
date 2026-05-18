#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#define MAX_EVENT 1024      // 最大客户端连接数
#define SERV_PORT 9999
#define BUFLEN 8000

void recvdata(int fd, int events, void *arg);
void senddata(int fd, int events, void *arg);

struct myevent_s {
    int fd;
    int events;
    void *arg;
    void (*call_back)(int fd, int events, void *arg);
    int status;               // 0: 不在epoll中, 1: 在epoll中
    char buf[BUFLEN];
    int len;                  // 当前缓冲区数据长度（接收时有效，发送时表示待发送字节数）
    int offset;               // 发送时已发送的偏移量（用于部分发送）
    long last_active;
};

int g_efd;
struct myevent_s g_events[MAX_EVENT + 1];   // 最后一个位置给监听socket

void eventset(struct myevent_s *ev, int fd, void(*call_back)(int, int, void*), void *arg) {
    ev->fd = fd;
    ev->call_back = call_back;
    ev->events = 0;
    ev->arg = arg;
    ev->status = 0;
    memset(ev->buf, 0, sizeof(ev->buf));
    ev->len = 0;
    ev->offset = 0;
    ev->last_active = time(NULL);
}

// 添加/修改事件到epoll（自动判断ADD或MOD）
void eventadd(int efd, int events, struct myevent_s *ev) {
    struct epoll_event epv = {0, {0}};
    int op;
    epv.data.ptr = ev;
    epv.events = ev->events = events;

    if (ev->status == 0) {
        op = EPOLL_CTL_ADD;
        ev->status = 1;
    } else {
        op = EPOLL_CTL_MOD;
    }

    if (epoll_ctl(efd, op, ev->fd, &epv) < 0) {
        printf("event add/mod failed [fd=%d], events[%d]: %s\n", ev->fd, events, strerror(errno));
    } else {
        printf("event add/mod OK [fd=%d], op=%d, events[%0X]\n", ev->fd, op, events);
    }
}

// 从epoll中删除事件
void eventdel(int efd, struct myevent_s *ev) {
    if (ev->status != 1) return;
    struct epoll_event epv = {0, {0}};
    ev->status = 0;
    if (epoll_ctl(efd, EPOLL_CTL_DEL, ev->fd, &epv) < 0) {
        printf("event del failed [fd=%d]: %s\n", ev->fd, strerror(errno));
    } else {
        printf("event del OK [fd=%d]\n", ev->fd);
    }
}

void acceptconnect(int lfd, int events, void *arg) {
    struct sockaddr_in clie_addr;
    socklen_t len = sizeof(clie_addr);
    int cfd, i;

    // 非阻塞 accept，循环处理直到 EAGAIN
    while (1) {
        cfd = accept(lfd, (struct sockaddr*)&clie_addr, &len);
        if (cfd == -1) {
            if (errno == EAGAIN || errno == EINTR) {
                // 没有新连接或中断，正常返回
                break;
            } else {
                printf("accept error: %s\n", strerror(errno));
                break;
            }
        }

        // 寻找空闲槽位（0 ~ MAX_EVENT-1）
        for (i = 0; i < MAX_EVENT; i++) {
            if (g_events[i].status == 0) break;
        }
        if (i == MAX_EVENT) {
            printf("max clients limit reached, close new connection\n");
            close(cfd);
            break;
        }

        // 设置非阻塞
        int flag = fcntl(cfd, F_SETFL, O_NONBLOCK);
        if (flag < 0) {
            printf("fcntl nonblock failed: %s\n", strerror(errno));
            close(cfd);
            break;
        }

        eventset(&g_events[i], cfd, recvdata, &g_events[i]);
        eventadd(g_efd, EPOLLIN, &g_events[i]);

        printf("new connect [%s:%d], fd=%d, pos=%d, time=%ld\n",
               inet_ntoa(clie_addr.sin_addr),
               ntohs(clie_addr.sin_port),
               cfd, i, g_events[i].last_active);
    }
}

void recvdata(int fd, int events, void *arg) {
    struct myevent_s *ev = (struct myevent_s*)arg;
    int len = recv(fd, ev->buf, sizeof(ev->buf), 0);

    if (len > 0) {
        // 成功收到数据
        ev->len = len;
        ev->offset = 0;
        ev->buf[len] = '\0';
        ev->last_active = time(NULL);   // 更新活跃时间

        printf("recv[fd=%d]: %s\n", fd, ev->buf);

        // 切换到发送状态
        eventdel(g_efd, ev);
        eventset(ev, fd, senddata, ev);
        eventadd(g_efd, EPOLLOUT, ev);
    } else if (len == 0) {
        // 对方关闭连接
        printf("fd=%d closed by peer\n", fd);
        close(ev->fd);
        eventdel(g_efd, ev);   // 从epoll中删除
        ev->status = 0;        // 标记空闲
    } else {
        // len < 0, 出错
        if (errno == EAGAIN || errno == EINTR) {
            // 非阻塞读暂无数据，重新添加读事件（之前可能被删除，这里确保重新添加）
            eventadd(g_efd, EPOLLIN, ev);
        } else {
            printf("recv error [fd=%d]: %s\n", fd, strerror(errno));
            close(ev->fd);
            eventdel(g_efd, ev);
            ev->status = 0;
        }
    }
}

void senddata(int fd, int events, void *arg) {
    struct myevent_s *ev = (struct myevent_s*)arg;
    int len = send(fd, ev->buf + ev->offset, ev->len - ev->offset, 0);

    if (len > 0) {
        ev->offset += len;
        ev->last_active = time(NULL);   // 更新活跃时间

        if (ev->offset >= ev->len) {
            // 全部发送完毕，切换回读状态
            printf("send[fd=%d] complete: %s\n", fd, ev->buf);
            eventdel(g_efd, ev);
            eventset(ev, fd, recvdata, ev);
            eventadd(g_efd, EPOLLIN, ev);
        } else {
            // 部分发送，继续等待写事件
            printf("send[fd=%d] partial: %d/%d bytes\n", fd, ev->offset, ev->len);
            eventadd(g_efd, EPOLLOUT, ev);
        }
    } else if (len == 0) {
        // 连接正常关闭？send不应返回0，按错误处理
        printf("send returned 0, closing fd=%d\n", fd);
        close(ev->fd);
        eventdel(g_efd, ev);
        ev->status = 0;
    } else {
        if (errno == EAGAIN || errno == EINTR) {
            // 写缓冲区满，继续等待写事件
            eventadd(g_efd, EPOLLOUT, ev);
        } else {
            printf("send error [fd=%d]: %s\n", fd, strerror(errno));
            close(ev->fd);
            eventdel(g_efd, ev);
            ev->status = 0;
        }
    }
}

void initlistenfd(int efd, short port) {
    struct sockaddr_in serv_addr;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) {
        printf("socket create failed: %s\n", strerror(errno));
        exit(1);
    }

    // 设置地址重用
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    fcntl(lfd, F_SETFL, O_NONBLOCK);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(lfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("bind failed: %s\n", strerror(errno));
        close(lfd);
        exit(1);
    }

    if (listen(lfd, 128) < 0) {
        printf("listen failed: %s\n", strerror(errno));
        close(lfd);
        exit(1);
    }

    // 监听socket放在 g_events[MAX_EVENT] 位置
    eventset(&g_events[MAX_EVENT], lfd, acceptconnect, &g_events[MAX_EVENT]);
    eventadd(efd, EPOLLIN, &g_events[MAX_EVENT]);

    printf("listen socket fd=%d, port=%d\n", lfd, port);
}

int main(int argc, char *argv[]) {
    unsigned short port = SERV_PORT;
    if (argc == 2) {
        port = atoi(argv[1]);
    }

    g_efd = epoll_create(1);   // 参数已忽略，随便给个正数
    if (g_efd < 0) {
        printf("epoll_create error: %s\n", strerror(errno));
        return 1;
    }

    initlistenfd(g_efd, port);

    struct epoll_event events[MAX_EVENT + 1];
    printf("server running: port=%d\n", port);

    int checkpos = 0;
    while (1) {
        // 超时检测（每循环一次扫描所有客户端）
        long now = time(NULL);
        for (int i = 0; i < MAX_EVENT; i++) {
            if (checkpos >= MAX_EVENT) checkpos = 0;
            if (g_events[checkpos].status == 1) {
                long duration = now - g_events[checkpos].last_active;
                if (duration >= 60) {
                    printf("fd=%d timeout, close\n", g_events[checkpos].fd);
                    close(g_events[checkpos].fd);
                    eventdel(g_efd, &g_events[checkpos]);
                    g_events[checkpos].status = 0;
                }
            }
            checkpos++;
        }

        int nfd = epoll_wait(g_efd, events, MAX_EVENT + 1, 1000);
        if (nfd < 0) {
            printf("epoll_wait error: %s\n", strerror(errno));
            break;
        }

        for (int i = 0; i < nfd; i++) {
            struct myevent_s *ev = (struct myevent_s*)events[i].data.ptr;
            // 按照就绪的事件类型调用相应的回调（回调内部会进一步判断具体事件）
            if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) {
                ev->call_back(ev->fd, events[i].events, ev->arg);
            }
            if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT)) {
                ev->call_back(ev->fd, events[i].events, ev->arg);
            }
        }
    }

    close(g_efd);
    return 0;
}
#include "wrap.h"

void perr_exit(const char *str) {
    perror(str);
    exit(1);
}

int Accept(int fd, struct sockaddr *sa, socklen_t *salenptr) {
    int n;
again:
    if ((n = accept(fd, sa, salenptr)) < 0) {
        if (errno == ECONNABORTED || errno == EINTR)
            goto again;
        else
            perr_exit("accept error");
    }
    return n;
}

int Bind(int fd, const struct sockaddr *sa, socklen_t salen) {
    if (bind(fd, sa, salen) < 0)
        perr_exit("bind error");
    return 0;
}

int Connect(int fd, const struct sockaddr *sa, socklen_t salen) {
    if (connect(fd, sa, salen) < 0)
        perr_exit("connect error");
    return 0;
}

int Listen(int sockfd, int backlog) {
    if (listen(sockfd, backlog) < 0)
        perr_exit("listen error");
    return 0;
}

int Socket(int domain, int type, int protocol) {
    int n = socket(domain, type, protocol);
    if (n < 0)
        perr_exit("socket error");
    return n;
}

ssize_t Read(int fd, void *ptr, size_t nbytes) {
    ssize_t n;
again:
    if ((n = read(fd, ptr, nbytes)) == -1) {
        if (errno == EINTR)
            goto again;
        else
            return -1;
    }
    return n;
}

// 带EINTR重试的单次write
ssize_t Write(int fd, const void *ptr, size_t nbytes) {
    ssize_t n;
again:
    if ((n = write(fd, ptr, nbytes)) == -1) {
        if (errno == EINTR)
            goto again;
        else
            return -1;
    }
    return n;
}

int Close(int fd) {
    if (close(fd) < 0)
        perr_exit("close error");
    return 0;
}

// 读取n个字节
ssize_t Readn(int fd, void *vptr, size_t n) {
    size_t nleft = n;
    ssize_t nread;
    char *ptr = vptr;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else
                return -1;
        } else if (nread == 0) {
            break;
        }
        nleft -= nread;
        ptr += nread;
    }
    return n - nleft;
}

// 写入n个字节（保证全部写完）
ssize_t Writen(int fd, const void *vptr, size_t n) {
    size_t nleft = n;
    ssize_t nwritten;
    const char *ptr = vptr;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR)
                continue;
            else
                return -1;
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    return n;
}

static ssize_t my_read(int fd, char *ptr) {
    static int read_cnt = 0;
    static char *read_ptr;
    static char read_buf[100];
    if (read_cnt <= 0) {
again:
        if ((read_cnt = read(fd, read_buf, sizeof(read_buf))) < 0) {
            if (errno == EINTR)
                goto again;
            return -1;
        } else if (read_cnt == 0) {
            return 0;
        }
        read_ptr = read_buf;
    }
    read_cnt--;
    *ptr = *read_ptr++;
    return 1;
}

ssize_t Readline(int fd, void *vptr, size_t maxlen) {
    ssize_t n, rc;
    char c, *ptr = vptr;
    for (n = 1; n < maxlen; n++) {
        if ((rc = my_read(fd, &c)) == 1) {
            *ptr++ = c;
            if (c == '\n')
                break;
        } else if (rc == 0) {
            *ptr = 0;
            return n - 1;
        } else {
            return -1;
        }
    }
    *ptr = 0;
    return n;
}
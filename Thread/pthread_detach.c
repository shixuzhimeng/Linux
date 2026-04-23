#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* th_fun(void *arg) {
    int i;
    int sum = 0;
    for(i = 1; i <= 100; i++) {
        sum += i;
    }
    return (void*)sum;
}

void out_state(pthread_attr_t *attr) {
    int detach_state;
    pthread_attr_getdetachstate(attr, &detach_state);
    if (detach_state == PTHREAD_CREATE_JOINABLE)
        printf("Attribute: joinable\n");
    else if (detach_state == PTHREAD_CREATE_DETACHED)
        printf("Attribute: detached\n");
    else
        printf("Attribute: unknown\n");
}

int main() {
    int err;
    pthread_t default_th, detach_th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    out_state(&attr);

    if((err = pthread_create(&default_th, &attr, th_fun, (void*)0)) != 0) {
        perror("failed\n");
    }
    int res;
    if((err = pthread_join(default_th, (void*)&res)) != 0) {
        perror("pthread_join failed\n");
    }
    else {
        printf("default is %d\n", (int)res);
    }
    printf("----------------------\n");

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if((err = pthread_create(&detach_th, &attr, th_fun, (void*)0)) != 0) {
        perror("failed\n");
    }
    //分离状态下调用pthread_join函数是失败的
    if((err = pthread_join(detach_th, (void*)&res)) != 0) {
        //perror("pthread_join failed\n");
        fprintf(stderr, "%s\n", strerrno(err));
    }
    else {
        printf("default return is %d\n", pthread_self());
    }
    
    pthread_attr_destroy(&attr);
    printf("0x%lx finished\n",pthread_self());
    sleep(1);
    
    return 0;
}
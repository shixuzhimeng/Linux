#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <stdint.h>
void* th_fn(void *arg) {
    int distance = (intptr_t)arg;
    int i;
    for(int i = 0; i < distance; i++) {
        printf("%lx run %d\n",pthread_self(), i);
        int time = (int)(drand48() * 100000);
        usleep(time);//微秒
    }
    return (void*)0;
}
int main () {
    int err;
    pthread_t rabbit, turtle;
    if((err = pthread_create(&rabbit, NULL, th_fn, (void*)50)) != 0) {
        perror("pthread_creat error");
    }
    if((err = pthread_create(&turtle, NULL, th_fn, (void*)50)) != 0) {
        perror("pthread_creat error");
    }
    //主控线程在调用pthread_join的时候会自己阻塞，直到rabbit和trutle执行结束后才会继续进行/
    pthread_join(rabbit, NULL);
    pthread_join(turtle, NULL);

    //sleep(10);
    printf("contral thread id = %lx\n", pthread_self());
    printf("finished\n");
    return 0;
}
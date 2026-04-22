#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
typedef struct {
    char name[20];
    int time;
    int start;
    int end;
}Race;

void *th_fn(void * arg) {
    Race *r = (Race*)arg;
    for(int i = r->start; i <= r->end; i++) {
        printf("%s(%lx) running %d\n", r->name, pthread_self(), i);
        usleep(10);
    }
    return (void*)0;
}

int main () {
    int err;
    pthread_t rabbit ,trutle;
    Race r = {"rabbit", (int)(drand48()*100000), 10, 50};
    Race t = {"trutle", (int)(drand48()*100000), 20, 60};
    if((err = pthread_create(&rabbit, NULL, th_fn, (void*)&r)) != 0) {
        perror("failed");
    }
    if((err = pthread_create(&trutle, NULL, th_fn, (void*)&t)) != 0) {
        perror("failed");
    }
    pthread_join(rabbit, NULL);
    pthread_join(trutle, NULL);
    printf("contral pthread is %lx", pthread_self());
    printf("finished");
    return 0;
}
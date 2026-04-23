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
        int time = (int)(drand48()*1000000);
        usleep(time);
    }
    //return (void*)0;
    return (void*)(r->end - r->start);
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
    
    // pthread_join(rabbit, NULL);
    // pthread_join(trutle, NULL);
    
    int result;
    pthread_join(rabbit, (void*)&result);
    printf("rabbit race is %d\n",result);
    pthread_join(trutle, (void*)&result);
    printf("trutle race is %d\n", result);
    printf("race finished\n");

    
    printf("contral pthread is %lx\n", pthread_self());
    printf("finished\n");
    return 0;
}
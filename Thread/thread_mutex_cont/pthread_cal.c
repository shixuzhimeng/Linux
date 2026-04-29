#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


typedef struct Result {
    int cal;
    int is_wait;
    pthread_cond_t cont;
    pthread_mutex_t mutex;
}Result;


void* set_fn(void* arg) {
    int i = 1;
    int sum = 0;
    for(; i <= 100; i++) {
        sum += i;
    }
    Result *r = (Result*)arg;

    r->cal = sum;
    pthread_mutex_lock(&r->mutex);
    while(!r->is_wait) {
        pthread_mutex_unlock(&r->mutex);
        usleep(100);
        pthread_mutex_lock(&r->mutex);

    }
    pthread_mutex_unlock(&r->mutex);
    pthread_cond_broadcast(&r->cont);

    int res = r->cal;
    printf("0x%lx get sum is %d\n", pthread_self(), res);


    return (void*)0;
}

void* get_fn(void* arg) {
    Result *r = (Result*)arg;
    
    pthread_mutex_lock(&r->mutex);
    r->is_wait = 1;
    pthread_cond_wait(&r->cont, &r->mutex);
    pthread_mutex_unlock(&r->mutex);
    return (void*)0;
}

int main () {
    int err;
    Result r;
    pthread_t cal, get;
    pthread_cond_init(&r.cont, NULL);
    pthread_mutex_init(&r.mutex, NULL);

    if((err = pthread_create(&get, NULL, get_fn, (void*)&r)) != 0) {
        printf("create failed\n");
    }

    if((err = pthread_create(&cal, NULL, set_fn, (void*)&r)) != 0) {
        printf("create failed\n");
    }

    pthread_join(cal, NULL);
    pthread_join(get, NULL);

    pthread_cond_destroy(&r.cont);
    pthread_mutex_destroy(&r.mutex);

    return 0;
}
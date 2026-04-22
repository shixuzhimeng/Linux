#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct {
    int d1;
    int d2;
}Arg;

void *th_fn(void *arg) {
    Arg *r = (Arg*) arg;
    return (void*)(r->d1 + r->d2);
}

int main () {
    int err;
    pthread_t th;
    Arg r = {20, 50};
    if((err = pthread_create(&th, NULL, th_fn, (void*)&r))!= 0) {
        perror("failed\n");
    }

    //强制类型转换
    // int *result;
    // pthread_join(th, (void**)&result);
    // printf("result is %d\n", (int)result);
    
    int result;
    pthread_join(th, (void*)&result);
    printf("result is %d\n", result);

    return 0;
}
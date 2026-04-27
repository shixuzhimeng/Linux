#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    int err;
    pthread_mutex_t mutex;
    if(argc < 2) {
        printf("-usage:%s [error|normal|recursive]\n", argv[0]);
        exit(1);
    }
    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    
    if(!strcmp(argv[1], "error")) {
        pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_ERRORCHECK);
    }
    else if(!strcmp(argv[1], "normal")) {
        pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
    }
    else if(!strcmp(argv[1], "recursive")){
        pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
    }

    pthread_mutex_init(&mutex, &mutexattr);

    if(pthread_mutex_lock(&mutex) != 0) {
        printf("lock failed\n");
    } else {
        printf("lock success\n");
    }

    if(pthread_mutex_lock(&mutex) != 0) {
        printf("lock failed\n");
    } else {
        printf("lock success\n");
    }

    pthread_mutex_unlock(&mutex);
    pthread_mutex_unlock(&mutex);
    
    pthread_mutexattr_destroy(&mutexattr);
    pthread_mutex_destroy(&mutex);

    return 0;
}
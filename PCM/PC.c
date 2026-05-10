#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

pthread_mutex_t mutex;
pthread_cond_t cond;
int wait = 0;
int data = 0;

void Push(int *a, int i) {
    a[i] = i;
}

void Pop(int *b, int i) {
    printf("%d ", b[i]);
}

void *Product(void *a) {
    int *A = a;
    int i = 0;
    while(1) {
        pthread_mutex_lock(&mutex);
        while(wait == 1) {
            pthread_cond_wait(&cond, &mutex);
        }
        if(i > 100) {
            i = 0;
        }
        Push(A, i);
        data = i;
        i++;
        wait = 1;
        pthread_mutex_unlock(&mutex);
        pthread_cond_signal(&cond);
    }
    return (void*)0;
}

void *Consumer(void *b) {
    int *B = b;
    while(1) {
        pthread_mutex_lock(&mutex);
        while(wait == 0) {
            pthread_cond_wait(&cond, &mutex);
        }
        Pop(B, data);
        wait = 0;
        pthread_mutex_unlock(&mutex);
        pthread_cond_signal(&cond);
    }
    return (void*)0;
}

int main() {
    pthread_t s,c;
    int arr[100];
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    pthread_create(&s, NULL, Product, arr);
    pthread_create(&c, NULL, Consumer, arr);

    pthread_join(s, NULL);
    pthread_join(c, NULL);

    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
    return 0;
}
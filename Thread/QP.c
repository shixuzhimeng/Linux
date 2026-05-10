#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

pthread_mutex_t mutex;
int total_ticket = 1000;

void *QP(void *arg) {
    int num = *(int*)arg;
    while(1) {
        pthread_mutex_lock(&mutex);
        if(total_ticket > 0) {
            total_ticket--;
            printf("线程%d 抢到了这张票 还剩%d张票\n", num, total_ticket);
            pthread_mutex_unlock(&mutex);
            usleep(10);
        }
        else {
            pthread_mutex_unlock(&mutex);
            break;
        }
    }
    return NULL;
}

int main() {
    pthread_t thread[10];
    int nums[10];

    for(int i = 0; i < 10; i++) {
        nums[i] = i + 1;
        pthread_create(&thread[i], NULL, QP, &nums[i]);
    }
    for(int i = 0; i < 10; i++) {
        pthread_join(thread[i], NULL);
    }

    return  0;
}
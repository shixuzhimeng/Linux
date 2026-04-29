#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int value;

    pthread_cond_t rc;
    pthread_mutex_t rm;
    int r_wait;

    pthread_cond_t wc;
    pthread_mutex_t wm;
    int w_wait;
}Storage;

void set_data(Storage *s, int value) {
    s->value = value;
}

int get_data(Storage *s) {
    return s->value;
}

void* set_th(void *arg) {
    Storage *s = (Storage*)arg;
    int i = 1;
    for(; i <= 100; i++) {
        //写入数据
        set_data(s, i+100); 
        printf("0x%lx write data: %d\n", pthread_self(), i);
        pthread_mutex_lock(&s->rm);
        //判断读者线程是否准备好
        while(!s->r_wait) {
            pthread_mutex_unlock(&s->rm);
            sleep(1);
            pthread_mutex_lock(&s->rm);
        }
        s->r_wait = 0;
        pthread_mutex_unlock(&s->rm);
        pthread_cond_broadcast(&s->rc);

        pthread_mutex_lock(&s->wm);
        s->w_wait = 1;
        pthread_cond_wait(&s->wc, &s->wm);
        pthread_mutex_unlock(&s->wm);
    }
    return (void*)0;
}

void* get_th(void *arg) {
    Storage *s = (Storage*)arg;
    int i = 1;
    for(; i <= 100; i++) {
        pthread_mutex_lock(&s->rm);
        s->r_wait = 1;
        pthread_cond_wait(&s->rc, &s->rm);
        pthread_mutex_unlock(&s->rm);

        //读者线程被唤醒后从Storage中读取数据
        int value = get_data(s);
        printf("0x%lx(%-5d) read data: %d\n", pthread_self(), i, value);
        pthread_mutex_lock(&s->wm);
        //判断写者线程是否准备好
        while(!s->w_wait) {
            pthread_mutex_unlock(&s->wm);
            sleep(1);
            pthread_mutex_lock(&s->wm);
        }
        s->w_wait = 0;
        pthread_mutex_unlock(&s->wm);
        pthread_cond_broadcast(&s->wc);
    }
    return (void*)0;
}

int main() {
    int err;
    pthread_t rth, wth;
    Storage s;
    s.r_wait = 0;
    s.w_wait = 0;
    
    pthread_mutex_init(&s.rm, NULL);
    pthread_mutex_init(&s.wm, NULL);

    pthread_cond_init(&s.rc, NULL);
    pthread_cond_init(&s.wc, NULL);

    if((err = pthread_create(&rth, NULL, get_th, (void*)&s)) != 0) {
        perror("failed\n");
    }
    if((err = pthread_create(&wth, NULL, set_th, (void*)&s)) != 0) {
        perror("failed\n");
    }

    pthread_join(rth, NULL);
    pthread_join(wth, NULL);

    pthread_mutex_destroy(&s.rm);
    pthread_mutex_destroy(&s.wm);

    pthread_cond_destroy(&s.rc);
    pthread_cond_destroy(&s.wc);

    return 0;
}



// #include <stdio.h>
// #include <pthread.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>

// typedef struct {
//     int value;

//     pthread_cond_t rc;
//     pthread_mutex_t rm;
//     int r_wait;      // 读者等待标志：1表示有数据可读，0表示无数据

//     pthread_cond_t wc;
//     pthread_mutex_t wm;
//     int w_wait;      // 写者等待标志：1表示数据已读走可写，0表示不可写
// } Storage;

// void set_data(Storage *s, int value) {
//     s->value = value;
// }

// int get_data(Storage *s) {
//     return s->value;
// }

// void* set_th(void *arg) {
//     Storage *s = (Storage*)arg;
//     for (int i = 1; i <= 100; i++) {
//         // 等待缓冲区可写（即上一次数据已被读走）
//         pthread_mutex_lock(&s->wm);
//         while (s->w_wait == 0) {   // w_wait=0 表示不可写（数据未读）
//             pthread_cond_wait(&s->wc, &s->wm);
//         }
//         // 写入数据
//         set_data(s, i + 100);
//         printf("0x%lx write data: %d\n", pthread_self(), i);
//         s->w_wait = 0;             // 数据已写，变为不可写（等待读）
//         pthread_mutex_unlock(&s->wm);

//         // 通知读者：有数据可读
//         pthread_mutex_lock(&s->rm);
//         s->r_wait = 1;             // 设置可读标志
//         pthread_cond_signal(&s->rc);
//         pthread_mutex_unlock(&s->rm);
//     }
//     return (void*)0;
// }

// void* get_th(void *arg) {
//     Storage *s = (Storage*)arg;
//     for (int i = 1; i <= 100; i++) {
//         // 等待数据可读
//         pthread_mutex_lock(&s->rm);
//         while (s->r_wait == 0) {
//             pthread_cond_wait(&s->rc, &s->rm);
//         }
//         // 读取数据
//         int value = get_data(s);
//         printf("0x%lx(%-5d) read data: %d\n", pthread_self(), i, value);
//         s->r_wait = 0;             // 数据已读，清除可读标志
//         pthread_mutex_unlock(&s->rm);

//         // 通知写者：缓冲区可写
//         pthread_mutex_lock(&s->wm);
//         s->w_wait = 1;             // 设置可写标志
//         pthread_cond_signal(&s->wc);
//         pthread_mutex_unlock(&s->wm);
//     }
//     return (void*)0;
// }

// int main() {
//     int err;
//     pthread_t rth, wth;
//     Storage s;
//     // 初始状态：写者可以写（w_wait=1），读者不能读（r_wait=0）
//     s.r_wait = 0;
//     s.w_wait = 1;
//     s.value = 0;

//     pthread_mutex_init(&s.rm, NULL);
//     pthread_mutex_init(&s.wm, NULL);

//     pthread_cond_init(&s.rc, NULL);
//     pthread_cond_init(&s.wc, NULL);

//     if((err = pthread_create(&rth, NULL, get_th, (void*)&s)) != 0) {
//         perror("failed\n");
//     }
//     if((err = pthread_create(&wth, NULL, set_th, (void*)&s)) != 0) {
//         perror("failed\n");
//     }

//     pthread_join(rth, NULL);
//     pthread_join(wth, NULL);

//     pthread_mutex_destroy(&s.rm);
//     pthread_mutex_destroy(&s.wm);

//     pthread_cond_destroy(&s.rc);
//     pthread_cond_destroy(&s.wc);

//     return 0;
// }
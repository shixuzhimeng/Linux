#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>

#define DEFAULT_TIME 10          //10秒检测仪一次
#define MIN_WAIT_TASK_NMU 10     //添加新的线程到线程池中
#define DEFAULT_THREAD_VARY 10   //每次创建或是销毁的个数
#define true 1
#define false 0

typedef struct {
    void *(*function)(void *);    //函数指针， 回调函数
    void *arg;                    //上面函数的参数
}threadpool_task_t;               //各个子线程的任务结构

typedef struct threadpool_t{
    pthread_mutex_t lock;            //用于锁住本结构体
    pthread_mutex_t thread_counter;  //记录忙状态的线程的个数的锁  

    pthread_cond_t queue_not_full;   //任务队列满时，添加任务的线程阻塞，等待这个条件变量
    pthread_cond_t queue_not_empty;  //任务队列不为空，通知等待任务的线程

    pthread_t *threads;              //存放每个线程tid的数组
    pthread_t adjust_tid;            //存管理线程的tid
    threadpool_task_t *task_queue;   //任务队列（数组的首地址）

    int min_thr_num;                 //线程池的最小线程数量
    int max_thr_num;                 //线程池的最大线程数量
    int live_thr_num;                //当前存活的线程数量
    int busy_thr_num;                //忙状态线程个数
    int wait_exit_thr_num;           //要销毁的线程个数

    int queue_front;                 //task_queue队头下标
    int queue_rear;                  //task_queue队尾下标
    int queue_size;                  //task_queue队中实际的任务个数
    int queue_max_size;              //task_queue队列可容纳的任务上限

    int shutdown;                    //标志位，线程使用状态，true/false
}threadpool_t;

void *threadpool_thread(void *threadpool);

void *adjust_thread(void *threadpool);

int is_thread_alive(pthread_t tid);

int pthreadpool_free(struct threadpool_t *pool);

void *threadpool_create(int min_thr_num, int max_thr_nim, int queue_max_size) {
    int i;
    threadpool_t *pool = NULL;
    do{
        if((pool = (threadpool_t *)malloc(sizeof(threadpool_t))) == NULL) {
            printf("malloc failed\n");
            break;
        }
        pool->min_thr_num = min_thr_num;
        pool->max_thr_num = max_thr_nim;
        pool->busy_thr_num = 0;
        pool->live_thr_num = min_thr_num;        //初值为最小的线程数
        pool->wait_exit_thr_num = 0;
        pool->queue_size = 0;                    //0个产品
        pool->queue_max_size = queue_max_size;   //最大任务队列
        pool->queue_front = 0;
        pool->queue_rear = 0;
        pool->shutdown = false;                  //不关闭线程池

        //根据最大线程数上限，给线程工作函数开辟空间并清零
        pool->threads = (pthread_t *)malloc(sizeof(pthread_t)*max_thr_nim);
        if(pool->threads == NULL) {
            printf("malloc failed\n");
            break;
        }
        memset(pool->threads, 0, sizeof(pthread_t*)*max_thr_nim);

        //给任务队列开辟空间
        pool->task_queue = (threadpool_task_t*)malloc(sizeof(threadpool_task_t));
        if(pool->task_queue == NULL) {
            printf("malloc failed\n");
            break;
        }

        //初始化互斥锁，条件变量
        if(pthread_mutex_init(&(pool->lock), NULL) != 0 
        || pthread_mutex_init(&(pool->thread_counter), NULL) != 0
        || pthread_cond_init(&(pool->queue_not_full), NULL) != 0
        || pthread_cond_init(&(pool->queue_not_empty), NULL) != 0) {
            printf("init the lock or cond fail\n");
            break;
        }

        //启动min个工作线程
        for(i = 0; i < min_thr_num; i++) {
            pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool);  //pool指向当前的线程池
            prinf("start pthread 0x%x...\n", (unsigned int)pool->threads[i]);
        }
        pthread_create(&(pool->adjust_tid), NULL, adjust_thread, (void *)pool);  //创建管理者线程
        return pool;
    }while(1);

    threadpool_free(pool);   //前面代码调用失败，释放pool的存储空间
    return NULL;
}

int threadpool_add(threadpool_t *pool, void*(*function)(void *arg), void *arg) {
    pthread_mutex_lock(&(pool->lock));
    
    //为真，队列满了，调用wait阻塞
    while((pool->queue_size == pool->queue_max_size) && (!pool->shutdown)){
        pthread_cond_wait(&(pool->queue_not_full), &(pool->lock));
    }
    if(pool->shutdown){
        pthread_cond_broadcast(&(pool->queue_not_empty));
        pthread_mutex_unlock(&(pool->lock));
        return 0;
    }
    //清空工作线程，调用回调函数的参数arg
    if(pool->task_queue[pool->queue_rear].arg != NULL) {
        pool->task_queue[pool->queue_rear].arg = NULL;
    }

    //添加任务到任务队列里面
    pool->task_queue[pool->queue_rear].function = function;
    pool->task_queue[pool->queue_rear].arg = arg;
    pool->queue_rear = (pool->queue_rear + 1) % pool->queue_max_size;    //队尾指针移动，模拟环形(算法)
    pool->queue_size++;       //任务队列里添加了一个任务

    //添加任务后，队列不为空，唤醒线程池中的等待处理任务的线程
    pthread_cond_signal(&(pool->queue_not_empty));
    pthread_mutex_unlock(&(pool->lock));

    return 0;
}


//线程池中各个工作线程
void *threadpool_thread(void *threadpool) {
    threadpool_t *pool = (threadpool_t*)threadpool;
    threadpool_task_t task;

    while(1) {
        //刚创建线程，有任务在等待队列里，否则阻塞等待任务队列里有了任务再唤醒就收任务
        pthread_mutex_lock(&(pool->lock));

        //queue_size = 0说明没有任务，调wait阻塞在条件变量上，若有任务，跳过循环
        while((pool->queue_size == 0) && (!pool->shutdown)) {
            printf("thread 0x%x is waiting\n", (unsigned int)pthread_self());
            pthread_cond_wait(&(pool->queue_not_empty), &(pool->lock));

            //清除指定数目的空闲线程，如果要结束的线程的个数大于0，结束线程
            if(pool->wait_exit_thr_num > 0) {
                pool->wait_exit_thr_num--;
                if(pool->live_thr_num > pool->min_thr_num) {
                    printf("thread 0x%x is exiting\n", (unsigned int)pthread_self());
                    pool->live_thr_num--;
                    pthread_mutex_unlock(&(pool->lock));
                    pthread_exit(NULL);
                }
            }
        }
        //如果指定了true，要关闭的线程池里每个线程，自行退出处理，销毁线程池
        if(pool->shutdown) {
            pthread_mutex_lock(&(pool->lock));
            printf("thread 0x%x is exiting\n", (unsigned int)pthread_self());
            pthread_detach(pthead_self());
            pthread_exit(NULL);    //线程自行结束
        }

        //从任务队列里获取任务，(一个出队操作)
        task.function = pool->task_queue[pool->queue_front].function;
        task.arg = pool->task_queue[pool->queue_front].arg;

        pool->queue_front = (pool->queue_front + 1) % pool->queue_max_size;
        pool->queue_size--;

        //通知可以有新的任务添加进来
        pthread_cond_broadcast(&(pool->queue_not_full));
    
        //任务取出后，立即将线程池锁释放
        pthread_mutex_unlock(&(pool->lock));
    
        //执行任务
        printf("thread 0x%x start working\n", (unsigned int)pthread_self());
        pthread_mutex_lock(&(pool->thread_counter));
        pool->busy_thr_num++;          //忙线程状态数加一
        pthread_mutex_unlock(&(pool->thread_counter));

        (*(task.function))(task.arg);   // 执行回调函数任务
        
        //任务结束处理
        printf("thread 0x%x end working", (unsigned int)pthread_self());
        pthread_mutex_lock(&(pool->thread_counter));
        pool->busy_thr_num--;          //忙线程状态数减一
        pthread_mutex_unlock(&(pool->thread_counter));

    }
    pthread_exit(NULL);
}


//管理线程
void *adjust_thread(void *threadpool) {
    int i;
    threadpool_t *pool = (threadpool_t *)threadpool;
    while(!pool->shutdown) {
        sleep(DEFAULT_TIME);                     //定时管理线程数
        pthread_mutex_lock(&(pool->lock));
        int queue_size = pool->queue_size;       //关注任务数
        int live_thr_num = pool->live_thr_num;   //存活线程数
        pthread_mutex_unlock(&(pool->lock));
    
        pthread_mutex_lock(&(pool->lock));
        int busy_thr_num = pool->busy_thr_num;   //忙着的线程数量
        pthread_mutex_unlock(&(pool->lock));


        //创建新线程算法：任务数大于最小线程池的个数，且存活的线程数少于最大线程个数的时候
        if(queue_size > MIN_WAIT_TASK_NMU && live_thr_num < pool->max_thr_num) {
            pthread_mutex_lock(&(pool->lock));
            int add = 0;

            //一次增加DEFAUL_THREAD个线程
            for(i = 0; i < pool->max_thr_num 
                && add < DEFAULT_THREAD_VARY 
                && pool->live_thr_num < pool->max_thr_num; i++) {
                    if(pool->threads[i] == 0 || !is_thread_alive(pool->threads[i])) {
                        pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool);
                        add++;
                        pool->live_thr_num++;
                    }
            }
            pthread_mutex_unlock(&(pool->lock));
        }
        //销毁多余的空闲线程的算法：忙线程*2 < 存活的线程数 且 存活的线程数 > 最小线程数时
        if((busy_thr_num * 2) < live_thr_num && live_thr_num > pool->min_thr_num) {
            pthread_mutex_lock(&(pool->lock));
            pool->wait_exit_thr_num = DEFAULT_THREAD_VARY;
            pthread_mutex_unlock(&(pool->lock));
            
            //通知空闲状态的线程，他们会自动停止
            for(i = 0; i < DEFAULT_THREAD_VARY; i++) {
                pthread_cond_signal(&(pool->queue_not_empty));
            }
        }
    }
    return NULL;
    
}

int threadpool_destroy(threadpool_t *pool) {
    int i;
    if(pool == NULL) {
        return -1;
    }
    pool->shutdown = true;
}

//模拟处理业务
void *process(void *arg) {
    printf("thread 0x%x working on task %d\n", (unsigned int)pthread_self(), (int)arg);
    sleep(1);
    printf("task %d is end\n", (int)arg);
    return NULL;
}

int main() {
    //创建线程池
    pthread_t *thp = threadpool_create(3, 100, 100);
    printf("pool inited");

    //int *num =  (int *)malloc(sizeof(int)*20);
    int num[20], i;
    for(i = 0; i < 20; i++) {
        num[i] = i;
        printf("add task%d\n", (void *)&num[i]);
        threadpool_add(thp, process, (void*)&num[i]);
    }
    sleep(10);
    threadpool_destroy(thp);
    return 0;
}
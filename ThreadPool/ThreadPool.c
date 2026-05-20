#include "ThreadPool.h"

Matrix* matrix_create(int rows, int cols) {
    Matrix *m = (Matrix*)malloc(sizeof(Matrix));
    if (!m) return NULL;
    m->rows = rows;
    m->cols = cols;
    m->data = (int*)calloc(rows * cols, sizeof(int));
    if (!m->data) {
        free(m);
        return NULL;
    }
    return m;
}

void matrix_free(Matrix *m) {
    if (m) {
        free(m->data);
        free(m);
    }
}

void matrix_random(Matrix *m) {
    for (int i = 0; i < m->rows * m->cols; ++i) {
        m->data[i] = rand() % 10;
    }
}

void matrix_multiply(const Matrix *a, const Matrix *b, Matrix *result) {
    if (a->cols != b->rows) {
        fprintf(stderr, "矩阵维度不匹配: %dx%d 与 %dx%d\n", a->rows, a->cols, b->rows, b->cols);
        return;
    }
    memset(result->data, 0, result->rows * result->cols * sizeof(int));
    for (int i = 0; i < a->rows; ++i) {
        for (int j = 0; j < b->cols; ++j) {
            int sum = 0;
            for (int k = 0; k < a->cols; ++k) {
                sum += a->data[i * a->cols + k] * b->data[k * b->cols + j];
            }
            result->data[i * result->cols + j] = sum;
        }
    }
}

int matrix_sum(const Matrix *m) {
    int s = 0;
    for (int i = 0; i < m->rows * m->cols; ++i) {
        s += m->data[i];
    }
    return s;
}

static void* worker_thread(void *arg) {
    ThreadPool *pool = (ThreadPool*)arg;
    while (1) {
        Task task;
        pthread_mutex_lock(&pool->lock);
        while (pool->queue_count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->cond_not_empty, &pool->lock);
        }
        if (pool->shutdown && pool->queue_count == 0) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }
        // 取出任务
        task = pool->task_queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;
        pool->queue_count--;
        pthread_cond_signal(&pool->cond_not_full);
        pthread_mutex_unlock(&pool->lock);

        // 安全检查
        if (!task.a || !task.b) {
            fprintf(stderr, "[任务 %d] 警告：输入矩阵为空，跳过执行\n", task.id);
            matrix_free(task.a);
            matrix_free(task.b);
            // 即使跳过，也要计数为已完成
            pthread_mutex_lock(&pool->lock);
            pool->tasks_completed++;
            if (pool->tasks_completed == pool->tasks_submitted) {
                pthread_cond_signal(&pool->cond_all_done);
            }
            pthread_mutex_unlock(&pool->lock);
            continue;
        }

        printf("[任务 %d] 开始执行，线程ID: %lu\n", task.id, (unsigned long)pthread_self());
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        Matrix *result = matrix_create(task.a->rows, task.b->cols);
        if (result) {
            matrix_multiply(task.a, task.b, result);
            clock_gettime(CLOCK_MONOTONIC, &end);
            double duration_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                                 (end.tv_nsec - start.tv_nsec) / 1000000.0;
            printf("[任务 %d] 完成，耗时: %.2f ms, 结果矩阵元素和: %d\n",
                   task.id, duration_ms, matrix_sum(result));
            if (task.callback) {
                task.callback(result, task.a, task.b, task.id);
            }
            matrix_free(result);
        } else {
            fprintf(stderr, "[任务 %d] 错误：无法创建结果矩阵，内存不足\n", task.id);
        }
        matrix_free(task.a);
        matrix_free(task.b);

        // 更新完成计数，如果所有任务已完成则唤醒等待的主线程
        pthread_mutex_lock(&pool->lock);
        pool->tasks_completed++;
        if (pool->tasks_completed == pool->tasks_submitted) {
            pthread_cond_signal(&pool->cond_all_done);
        }
        pthread_mutex_unlock(&pool->lock);
    }
    return NULL;
}

ThreadPool* thread_pool_create(int num_threads, int queue_capacity) {
    ThreadPool *pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    if (!pool) return NULL;
    pool->thread_count = num_threads;
    pool->queue_capacity = queue_capacity;
    pool->task_queue = (Task*)malloc(sizeof(Task) * queue_capacity);
    if (!pool->task_queue) {
        free(pool);
        return NULL;
    }
    pool->queue_head = 0;
    pool->queue_count = 0;
    pool->shutdown = 0;
    pool->tasks_submitted = 0;
    pool->tasks_completed = 0;
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond_not_empty, NULL);
    pthread_cond_init(&pool->cond_not_full, NULL);
    pthread_cond_init(&pool->cond_all_done, NULL);

    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        free(pool->task_queue);
        free(pool);
        return NULL;
    }
    for (int i = 0; i < num_threads; ++i) {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
    }
    return pool;
}

void thread_pool_submit(ThreadPool *pool, int id, Matrix *a, Matrix *b, TaskCallback callback) {
    pthread_mutex_lock(&pool->lock);
    while (pool->queue_count == pool->queue_capacity && !pool->shutdown) {
        pthread_cond_wait(&pool->cond_not_full, &pool->lock);
    }
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        matrix_free(a);
        matrix_free(b);
        fprintf(stderr, "线程池已关闭，任务 %d 被丢弃\n", id);
        return;
    }
    int tail = (pool->queue_head + pool->queue_count) % pool->queue_capacity;
    pool->task_queue[tail].id = id;
    pool->task_queue[tail].a = a;
    pool->task_queue[tail].b = b;
    pool->task_queue[tail].callback = callback;
    pool->queue_count++;
    pool->tasks_submitted++;   // 增加已提交计数
    pthread_cond_signal(&pool->cond_not_empty);
    pthread_mutex_unlock(&pool->lock);
}

// 等待所有已提交任务完成
void thread_pool_wait(ThreadPool *pool) {
    pthread_mutex_lock(&pool->lock);
    while (pool->tasks_completed < pool->tasks_submitted) {
        pthread_cond_wait(&pool->cond_all_done, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

void thread_pool_shutdown(ThreadPool *pool) {
    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond_not_empty);
    pthread_cond_broadcast(&pool->cond_not_full);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->thread_count; ++i) {
        pthread_join(pool->threads[i], NULL);
    }
    free(pool->threads);
    free(pool->task_queue);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond_not_empty);
    pthread_cond_destroy(&pool->cond_not_full);
    pthread_cond_destroy(&pool->cond_all_done);
    free(pool);
}

void my_callback(Matrix *result, Matrix *a, Matrix *b, int task_id) {
    printf("[回调] 任务 %d 的结果矩阵 (%d x %d) 元素和 = %d\n",
           task_id, result->rows, result->cols, matrix_sum(result));
}

int main() {
    srand(time(NULL));
    int thread_num = 10;
    int queue_capacity = 20;
    ThreadPool *pool = thread_pool_create(thread_num, queue_capacity);
    if (!pool) {
        perror("创建线程池失败");
        return 1;
    }
    printf("线程池创建成功：%d 个线程，队列容量 %d\n", thread_num, queue_capacity);

    const int TASK_COUNT = 30;
    for (int i = 1; i <= TASK_COUNT; ++i) {
        int rows = 5 + rand() % 6;
        int k = 5 + rand() % 6;
        int cols = 5 + rand() % 6;
        Matrix *a = matrix_create(rows, k);
        Matrix *b = matrix_create(k, cols);
        if (!a || !b) {
            fprintf(stderr, "创建矩阵失败\n");
            break;
        }
        matrix_random(a);
        matrix_random(b);
        printf("提交任务 %d: %dx%d * %dx%d\n", i, rows, k, k, cols);
        thread_pool_submit(pool, i, a, b, my_callback);
    }

    printf("\n所有任务已提交，等待执行完成...\n");
    thread_pool_wait(pool);   // 等待所有任务完成，无需 sleep

    // 关闭线程池（此时队列已空，工作线程会自然退出）
    thread_pool_shutdown(pool);
    printf("线程池已关闭，主程序退出。\n");
    return 0;
}
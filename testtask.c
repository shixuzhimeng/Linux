#include <stdio.h>
#include <unistd.h>

#define Task_NUM 10

typedef void(*task_t)();

task_t tasks[Task_NUM];

void task1() {
    printf("这是一个执行打印日志的任务\n");
}

void task2() {
    printf("这是一个执行检测网络间康的任务");
}

void task3() {
    printf("这是一个进行绘制图形界面的任务");
}

void InitTask() {
    for(int i = 0; i < Task_NUM; i++) 
    tasks[i] = NULL;
}

int AddTask(task_t) {
    int pos = 0;
    for(int i = 0; i < Task_NUM; i++) {
        if(!tasks[pos])
        break;
        if(pos == Task_NUM)
        return 0;
    }
}

void ExecuteTask() {
    for(int i= 0; i < Task_NUM; i++) {
        if(!tasks[i]) {
            continue;
        }
        tasks[i]();
    }
}
// struct task_struct {
//     // 进程ID
//     pid_t pid;

//     // 状态
//     volatile long state;

//     // 进程优先级
//     int prio;

//     // 指向该进程所在的运行队列
//     struct rq *rq;

//     // 虚拟内存空间
//     struct mm_struct *mm;

//     // 父进程、子进程链表
//     struct task_struct *parent;
//     struct list_head children;

//     // 进程调度信息
//     ...
// };

// struct runqueue {
//     task_struct** run;
//     task_struct** wait;
//     task_struct* running[140];
//     task_struct* waiting[140];
//     //0-99其他种类用的
//     //指针数组
// }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// int main () {
//     printf("PATH: %s\n", getenv("PATH"));
//     printf("USER: %s\n", getenv("USER"));

//     char who[32];
//     strcpy(who, getenv("USER"));

//     if(strcmp(who, "root") == 0) {
//         printf("可以做任何事情\n");
//     } else {
//         printf("需要权限\n");
//     }

//     return 0;
// }

int main (int argc, char *argv[], char *env[]) {
    if(strcmp(1, "cd") == 0) {
        chdir(argv[1]);
    }
    sleep(30);
    printf("change begin\n");
    if(argc == 2) {
        chdir(argv[1]);
    }
    printf("change begin\n");
    sleep(30);
    // if(argc != 2) {
    //     printf("Usage: %s -[a|b|c|d]\n", argv[0]);
    //     return 0;
    // }
    // else if(strcmp(argv[1], "-a") == 0) {
    //     printf("功能一\n");
    // }
    // else if(strcmp(argv[1], "-b") == 0) {
    //     printf("功能二\n");
    // }
    // else if(strcmp(argv[1], "-c") == 0) {
    //     printf("功能三\n");
    // }
    // else if(strcmp(argv[1], "-d") == 0) {
    //     printf("功能四\n");
    // }
    // else {
    //     printf("default功能\n");
    // }

    // int i = 0;
    // for(; i < argc; i++) {
    //     printf("argv[%d]->%s\n", i, argv[i]);
    // }
    return 0;
}
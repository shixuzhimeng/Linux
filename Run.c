struct task_struct {
    // 进程ID
    pid_t pid;

    // 状态
    volatile long state;

    // 进程优先级
    int prio;

    // 指向该进程所在的运行队列
    struct rq *rq;

    // 虚拟内存空间
    struct mm_struct *mm;

    // 父进程、子进程链表
    struct task_struct *parent;
    struct list_head children;

    // 进程调度信息
    ...
};

struct runqueue {
    task_struct** run;
    task_struct** wait;
    task_struct* running[140];
    task_struct* waiting[140];
    //0-99其他种类用的
    //指针数组
}
#include <stdio.h>
#include <unistd.h>
// 以路径名+参数列表的方式执行
//int execl(const char *path, const char *arg, ...);

// 以路径名+参数数组的方式执行  
//int execv(const char *path, char *const argv[]);

// 以文件名+参数列表的方式执行（在PATH中搜索）
//int execlp(const char *file, const char *arg, ...);

// 以文件名+参数数组的方式执行（在PATH中搜索）
//int execvp(const char *file, char *const argv[]);

// 以路径名+参数列表+环境变量的方式执行
//int execle(const char *path, const char *arg, ..., char *const envp[]);

// 以路径名+参数数组+环境变量的方式执行
//int execve(const char *path, char *const argv[], char *const envp[]);

int main () {
    printf("before: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
    execl("/usr/bin/ls", "ls", "-a", "-l", NULL);
    execl("/usr/bin/top", "top", NULL);
    
    printf("after: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
    return 0;
}
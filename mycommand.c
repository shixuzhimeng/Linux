#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

// int main() {
//     printf("before: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     execl("/usr/bin/ls", "ls", "-a", "-l", NULL);
//     execl("/usr/bin/top", "top", NULL);
    
//     printf("after: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     return 0;
// }

// int main() {
//     printf("before: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     execlp("ls", "ls", "-a", "-l", NULL);
    
//     printf("after: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     return 0;
// }

// int main() {
//     char const* myargv[] = {
//         "ls",
//         "-l",
//         "-a",
//         NULL
//     };

//     printf("before: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     execlv("usr/pin/ls", myargv);
    
//     printf("after: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     return 0;
// }

// int main() {
//     char const* myargv[] = {
//         "ls",
//         "-l",
//         "-a",
//         NULL
//     };

//     printf("before: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     execlv("ls", myargv);
    
//     printf("after: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     return 0;
// }

// int main() {
//     putenv();

//     char const* myargv[] = {
//         "ls",
//         "-l",
//         "-a",
//         NULL
//     };

//     printf("before: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     execlv("ls", myargv);
    
//     printf("after: I am a process, pid:%d, ppid:%d\n", getpid(), getppid());
//     return 0;
// }


int main() {
    putenv("M_NUM=123");

    printf("%s", getenv("M_NUM"));

    //删除环境变量
    putenv("M_NUM=");
}
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

int main () {
    pid_t id = fork();
    if(id < 0) {
        perror("fork");
        return 1;
    }
    else if(id == 0) {
        int cnt = 5;
        while(cnt) {
            printf("I am child, pid:%d, ppid:%d, cnt:%d\n", getpid(), getppid(), cnt);
            cnt--;
            sleep(1);
        }
        exit(0);
    }
    else {
        int cnt = 5;
        while(1) {
            printf("I am father, pid:%d, ppid:%d, cnt:%d\n", getpid(), getppid(), cnt);
            cnt--;
            sleep(1);
        }
    }
    int status = 0;
    pid_t ret = waitpid(id, &status, 0);
    if(ret == id) {
        printf("wait success, ret:%d\n",ret);
    }

    // pid_t ret = wait(NULL);
    // if(ret == id) {
    //     printf("wait success, ret: %d\n", ret);
    // }

    return 0;
}
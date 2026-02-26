#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <unistd.h> 

#define LEFT "["
#define RIGHT "]"
#define DELIM  " \t"

#define LINE_SIZE 1024
#define ARGC_SIZE 32
#define EXIT_CODE 44

#define NONE         "\033[m"
#define GREEN        "\033[1;32;32m"
#define BLUE         "\033[1;32;34m"

char commandline[LINE_SIZE];
char *argv[ARGC_SIZE];
char pwd[LINE_SIZE];
char myenv[LINE_SIZE];

int lastcode = 1;

const char *getusername() {
    return getenv("USER");
}

const char *getmyhostname() {
    return getenv("HOSTNAME");
}

void getpwd() {
    getcwd(pwd, sizeof(pwd));
}

void interact(char *cline, int size) {
    getpwd();
    printf(GREEN LEFT"%s@%s"RIGHT NONE":" BLUE"%s"NONE "$ ", getusername(), getmyhostname(), pwd);
    char *a = fgets(cline, size, stdin);
    assert(a != NULL);
    (void)a;
    cline[strlen(cline)-1] = '\0';
}

int splitstring(char *cline, char *Argv[]) {
    int i = 0;
    Argv[i++] = strtok(cline, DELIM);
    while(Argv[i++] = strtok(NULL, DELIM));
    return i - 1;
}

void NormalExcute(char *Argv[]) {
    pid_t id = fork();
        if(id < 0) {
            perror("fork");
            return;
        }
        else if(id == 0) {
            //子进程执行命令
            execvpe(Argv[0], Argv, environ);
            exit(EXIT_CODE);
        }
        else {
            //父进程等待子进程结束
            int status = 0;
            pid_t rid = waitpid(id, &status, 0);
            if(rid == id) {
                lastcode = WEXITSTATUS(status);
            }
        }
}

int BuildCommand(char *Argv[], int Argc) {
    if(Argc == 2 && strcmp(Argv[0], "cd") == 0) {
        getpwd();
        chdir(argv[1]);
        sprintf(getenv("PWD"), "%s", pwd);
        //putenv(argv[1]);
        return 1;
    }
    else if(Argc == 2 && strcmp(Argv[0], "export") == 0) {
        strcpy(myenv, Argv[1]);
        putenv(myenv);
        return 1;
    }
    else if(Argc == 2 && strcmp(Argv[0], "echo") == 0) {
        if(strcmp(Argv[1], "$?") == 0) {
            printf("%d\n", lastcode);
            lastcode = 0;
        }
        else if(*Argv[1] == '$') {
            char *val = getenv("Argv[1]+1");
            if(val) {
                printf("%s\n", val);
            }
        }
        else {
            printf("%s\n", Argv[1]);
        }
        return 1;
    }
    if(strcmp(Argv[0], "ls") == 0) {
        Argv[Argc++] = "--color";
        Argv[Argc] = NULL;
    }
    
    return 0;
}

int main() {
    extern char **environ;
    
    int quit= 0;
    int i = 0;
    
    while(!quit) {
        interact(commandline, sizeof(commandline));
        int argc = splitstring(commandline, argv);
        if(argc == 0) {
            continue;
        }
        //显示
        // for(int i = 0; argv[i]; i++) {
        //     printf("[%d]:%s\n", i, argv[i]);
        // }
        int n = BuildCommand(argv, argc);
        if(!n) {
            NormalExcute(argv);
        }
    }
    
    return 0;
}
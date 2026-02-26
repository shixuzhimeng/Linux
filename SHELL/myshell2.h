#ifndef MYSHELL2_H
#define MYSHELL2_H

#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>


#define LEFT "["
#define RIGHT "]"
#define DELIM  " \t"

#define LINE_SIZE 1024
#define ARGC_SIZE 64
#define MAX_PIPELINE 16


#define NONE         "\033[m"
#define GREEN        "\033[1;32;32m"
#define BLUE         "\033[1;32;34m"
#define RED          "\033[1;31m"

extern volatile sig_atomic_t sigint_received;

typedef enum RedirType{
    REDIR_NONE,
    REDIR_INPUT,    // <
    REDIR_OUTPUT,   // >
    REDIR_APPEND    // >>
}RedirType;


typedef struct RedirInfo{
    RedirType type;
    char *file;
}RedirInfo;


typedef struct Command{
    char **argv;
    int argc;
    int background;
    RedirInfo inputredir;
    RedirInfo outputredir;
}Command;


extern char commandline[LINE_SIZE];
extern char pwd[LINE_SIZE];
extern char myenv[LINE_SIZE];
extern int lastcode;
extern char **environ;


const char *getusername();
const char *getmyhostname();
void getpwd();
void Interact(char *cline, int size);
int ParseCommand(char *cline, Command *commands[], int *commandcount);
int HandleRedirections(Command *command);
int isCommand(Command *command);
int BuildCommand(Command *command);
void ExecutePipeline(Command *commands[], int commandcount);
void ExecuteSimple(Command *command);
void ExecuteCommand(Command *commands[], int commandcount);
void FreeCommand(Command *command);
void sigint_handler(int sig);
void setup_signal_handlers();

#endif
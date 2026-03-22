#include "myshell2.h"

volatile sig_atomic_t sigint_received = 0;
char commandline[LINE_SIZE];
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

//交互界面
void Interact(char *cline, int size) {
    // 检查信号
    if(sigint_received) {
        sigint_received = 0;
        printf("\n");
        // 重新获取当前目录（因为可能已经在其他地方更新了）
        getpwd();
    }
    printf(GREEN LEFT"%s@%s"RIGHT NONE":" BLUE"%s"NONE "$ ", getusername(), getmyhostname(), pwd);
    fflush(stdout);
    char *a = fgets(cline, size, stdin);
    // 检查是否被信号中断
    if(a == NULL) {
        if(sigint_received) {
            sigint_received = 0;
            clearerr(stdin);  // 清除错误标志
            // 重新显示提示符
            Interact(cline, size);
            return;
        }
        // EOF (Ctrl+D)
        if(feof(stdin)) {
            printf("\n");
            exit(0);
        }
        return;
    }
    
    // 去掉换行符
    size_t len = strlen(cline);
    if(len > 0 && cline[len-1] == '\n') {
        cline[len-1] = '\0';
    }
}

//命令解析
int ParseCommand(char *cline, Command *commands[], int *commandcount) {
    char _cline[LINE_SIZE];
    strcpy(_cline, cline);

    //检查是否有后台运行
    int background = 0;
    char *find = strchr(_cline, '&');
    if(find) {
        char *last = NULL;
        char *tmp = _cline;
        while((tmp = strchr(tmp, '&')) != NULL) {
            last = tmp;
            tmp++;
        }
        if(last) {
            char *_last = last + 1;
            while(*_last && isspace(*_last)) {
                _last++;
            }
            if(*_last == '\0') {
                background = 1;
                *last = '\0';
            }
        }
    }

    //按照管道分割
    char *pipesaveptr;
    char *cut1 = strtok_r(_cline, "|", &pipesaveptr);
    int count = 0;

    //处理每个管道
    while(cut1 && count < MAX_PIPELINE) {
        while(isspace(*cut1)) {
            cut1++;
        }
        char *end = cut1 + strlen(cut1) - 1;
        while(end > cut1 && isspace(*end)) {
            end--;
        }
        *(end + 1) = '\0';

        if(strlen(cut1) == 0) {
            return -1;
        }

        Command *command = calloc(1, sizeof(Command));
        if(!command) {
            perror("calloc failed");
            return -1;
        }

        command->background = 0;
        command->inputredir.type = REDIR_NONE;
        command->outputredir.type = REDIR_NONE;

        char *reout = strchr(cut1, '>');
        if(reout) {
            int append = 0;
            char *file_start = NULL;  // 文件名起始位置
            
            // 检查是否是 >>
            if(*(reout + 1) == '>') {
                append = 1;
                file_start = reout + 2;  // 文件名从第二个 > 后面开始
                
                // 删除两个 > 字符
                char *src = reout + 2;
                char *dst = reout;
                while(*src) {
                    *dst++ = *src++;
                }
                *dst = '\0';
            } 
            else {
                append = 0;
                file_start = reout + 1;  // 文件名从 > 后面开始
                *reout = '\0';  // 删除单个 >
            }
                
            while(isspace(*file_start)) {
                file_start++;
        }
    
        // 查找文件名结束位置
        char *file_end = file_start;
        while(*file_end && !isspace(*file_end) && *file_end != '|' && *file_end != '<') {
            file_end++;
        }
        
        // 提取文件名
        char tmp = *file_end;
        *file_end = '\0';
        
        command->outputredir.type = append ? REDIR_APPEND : REDIR_OUTPUT;
        command->outputredir.file = strdup(file_start);
        
        *file_end = tmp;
        }   

        char *rein = strchr(cut1, '<');
        if(rein) {
            char *file_s = rein + 1;
            while(isspace(*file_s)) {
                file_s++;
            }
            char *file_e = file_s;
            while (*file_e && !isspace(*file_e) && *file_e != '|' && *file_e != '>') {
                file_e++;
            }
            char tmp = *file_e;
            *file_e = '\0';
            command->inputredir.type = REDIR_INPUT;
            command->inputredir.file = strdup(file_s);

            *file_e = tmp;
            *rein = '\0';
        }
        

        char *cutsaveptr;
        char *cut2 = strtok_r(cut1, DELIM, &cutsaveptr);
        int _argc = 0;
        char **_argv = malloc(sizeof(char *) * (ARGC_SIZE + 1));

        while(cut2 && _argc < ARGC_SIZE) {
            _argv[_argc++] = cut2;
            cut2 = strtok_r(NULL, DELIM, &cutsaveptr);
        }
        _argv[_argc] = NULL;

        command->argv = _argv;
        command->argc = _argc;

        commands[count++] = command;
        cut1 = strtok_r(NULL, "|", &pipesaveptr);
    }
    if(background && count > 0) {
        commands[count - 1]->background = 1;
    }

    *commandcount = count;
    return 0;
}

int HandleRedirections(Command *command) {
    //输入重定向
    if(command->inputredir.type == REDIR_INPUT) {
        int fd = open(command->inputredir.file, O_RDONLY);
        if(fd < 0) {
            fprintf(stderr, RED"错误 :"NONE"无法打开输入文件 %s:%s\n", command->inputredir.file,strerror(errno));
            return -1;
        }
        if(dup2(fd, STDIN_FILENO) < 0) {
            perror(RED"dup2"NONE);
            close(fd);
            return -1;
        }
        close(fd);
    }
    //输出重定向
    if(command->outputredir.type == REDIR_OUTPUT) {
        int fd = open(command->outputredir.file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd < 0) {
            fprintf(stderr, RED"错误 :"NONE"无法打开输出文件 %s:%s\n", command->outputredir.file,strerror(errno));
            return -1;
        }
        if(dup2(fd, STDOUT_FILENO) < 0) {
            perror(RED"dup2"NONE);
            close(fd);
            return -1;
        }
        close(fd);
    }
    if(command->outputredir.type == REDIR_APPEND) {
        int fd = open(command->outputredir.file, O_WRONLY | O_CREAT |O_APPEND, 0644);
        if(fd < 0) {
            fprintf(stderr, RED"错误"NONE "无法打开输出文件 %s:%s\n", command->outputredir.file, strerror(errno));
            return -1;
        }
        if(dup2(fd, STDOUT_FILENO) < 0) {
            perror(RED"dup2"NONE);
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

int isCommand(Command *command) {
    if(!command || !command->argv || !command->argv[0]) {
        return 0;
    }
    char *name = command->argv[0];
    return (strcmp(name, "cd") == 0 || strcmp(name, "exit") == 0 || strcmp(name, "export") == 0 || strcmp(name, "echo") == 0);
}

int BuildCommand(Command *command) {
    if(command->argc == 0 || command->argv[0] == NULL) {
        return 0;
    }
    //处理cd命令
    if(strcmp(command->argv[0], "cd") == 0) {
        char _pwd[LINE_SIZE];
        getcwd(_pwd, sizeof(_pwd));
        char *path = NULL;
        if(command->argc == 1 || (command->argc >= 2 && strcmp(command->argv[1], "~") == 0)) {
            path = getenv("HOME");
            if(!path) 
                path = "/";
        }
        else if(command->argc >= 2 && strcmp(command->argv[1], "-") == 0) {
            path = getenv("OLDPWD");
            if(!path) {
                fprintf(stderr, "cd: OLDPWD not set\n");
                return 1;
            }
            printf("%s\n", path);
        }
        else {
            path = command->argv[1];
        }
        if(chdir(path) < 0) {
            perror("cd");
            return 0;
        }
        setenv("OLDPWD", _pwd, 1);
        getcwd(pwd, sizeof(pwd));
        setenv("PWD", pwd, 1);

        return 1;
    }

    //exit命令
    if(strcmp(command->argv[0], "exit") == 0) {
        int status = 0;
        if(command->argc > 1) {
            status = atoi(command->argv[1]);
        }
        printf("exit\n");
        exit(status);
    }

    //export命令
    if(strcmp(command->argv[0], "export") == 0) {
        if(command->argc == 1) {
            for(char **env = environ; *env; env++) {
                printf("%s\n", *env);
            }
        }
        else {
            for(int i = 1; i < command->argc; i++) {
                strcpy(myenv, command->argv[i]);
                putenv(myenv);
            }
        }
        return 1;
    }
    //echo命令
    if(strcmp(command->argv[0], "echo") == 0) {
        for(int i = 1; i < command->argc; i++) {
            if(strcmp(command->argv[i], "$?") == 0) {
                printf("%d ", lastcode);
            }
            else if(command->argv[i][0] == '$') {
                char *val = getenv(command->argv[i] + 1);
                if(val) {
                    printf("%s ", val);
                }
            }
            else {
                printf("%s ",command->argv[i]);
            }
        }
        printf("\n");
        lastcode = 0;
        return 1;
    }
    //ls命令
    if(strcmp(command->argv[0], "ls") == 0) {
        int color = 0;
        for(int i = 1; i < command->argc; i++) {
            if(strstr(command->argv[i], "--color") != NULL) {
                color = 1;
                break;
            }
        }
        
        if(!color && command->argc < ARGC_SIZE - 1) {
            command->argv[command->argc++] = "--color=always";
            command->argv[command->argc] = NULL;
        }
    }
    
    return 0;
}


void ExecutePipeline(Command *commands[], int commandcount) {
    // 调试信息
    // fprintf(stderr, "\n=== 管道调试信息 ===\n");
    // fprintf(stderr, "命令数量: %d\n", commandcount);
    // for(int i = 0; i < commandcount; i++) {
    //     fprintf(stderr, "命令 %d: ", i);
    //     for(int j = 0; commands[i]->argv[j]; j++) {
    //         fprintf(stderr, "[%s] ", commands[i]->argv[j]);
    //     }
    //     fprintf(stderr, "\n");
    // }
    // fprintf(stderr, "===================\n");
    // fflush(stderr);
    
    int pipefd[2];
    int prev_fd = -1;
    pid_t pids[MAX_PIPELINE];
    int i;
    int _background = commands[commandcount - 1]->background;

    for(i = 0; i < commandcount; i++) {
        if(i < commandcount - 1) {
            if(pipe(pipefd) < 0) {
                perror(RED"pipe"NONE);
                return ;
            }
        }
        pid_t pid = fork();
        if(pid < 0) {
            perror(RED"fork"NONE);
            return ;
        }
        if(pid == 0) {
            if(prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            if(i < commandcount - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
                close(pipefd[0]);
            }
            if(HandleRedirections(commands[i]) < 0) {
                exit(1);
            }
            execvp(commands[i]->argv[0], commands[i]->argv);
            exit(127);
        }
        pids[i] = pid;

        if(prev_fd != -1) {
            close(prev_fd);
        }
        if(i < commandcount - 1) {
            close(pipefd[1]);
            prev_fd = pipefd[0];
        }
    }
    if(_background) {
        printf("[%d] %d\n", 1, pids[0]);
        return ;
    }
    else {
        for(i = 0; i < commandcount; i++) {
            int status;
            waitpid(pids[i], &status, 0);
        }
    }
}

void ExecuteSimple(Command *command) {
    pid_t pid = fork();
    if(pid < 0) {
        perror(RED""NONE);
        return ;
    }
    if(pid == 0) {
        if(HandleRedirections(command) < 0) {
            exit(1);
        }
        execvp(command->argv[0], command->argv);
        fprintf(stderr, RED"%s: 没有找到命令"NONE "\n", command->argv[0]);
        exit(127);
    }
    if(command->background) {
        printf("[1] %d\n", pid);
    }
    else {
        int status;
        waitpid(pid, &status, 0);
    }
}
void ExecuteCommand(Command *commands[], int commandcount) {
    if(commandcount == 0) {
        return ;
    }
    
    int all_builtin = 1;
    for(int i = 0; i < commandcount; i++) {
        if(!isCommand(commands[i])) {
            all_builtin = 0;
            break;
        }
    }
    
    if(all_builtin && commandcount == 1) {
        Command *cmd = commands[0];

        int saved_stdout = dup(STDOUT_FILENO);

        if(cmd->outputredir.type != REDIR_NONE) {
            int flags = O_WRONLY | O_CREAT;
            if(cmd->outputredir.type == REDIR_OUTPUT) {
                flags |= O_TRUNC;
            } else {
                flags |= O_APPEND;
            }
            
            int fd = open(cmd->outputredir.file, flags, 0644);
            if(fd >= 0) {
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
        }

        BuildCommand(cmd);
        
        fflush(stdout);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
        
        return;
    }
    
    if(all_builtin && commandcount > 1) {
        for(int i = 0; i < commandcount; i++) {
            BuildCommand(commands[i]);
        }
        return;
    }
    
    ExecutePipeline(commands, commandcount);
}

void FreeCommand(Command *command) {
    if(!command) {
        return ;
    }
    if(command->argv) {
        free(command->argv);
    }
    if(command->inputredir.file) {
        free(command->inputredir.file);
    }
    if(command->outputredir.file) {
        free(command->outputredir.file);
    }

    free(command);
}

// 信号处理函数 - 只设置标志
void sigint_handler(int sig) {
    sigint_received = 1;
}

// 设置信号处理
void setup_signal_handlers() {
    struct sigaction sa;
    
    //SIGINT(Ctrl+C)
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    
    //SIGQUIT --- 忽略
    signal(SIGQUIT, SIG_IGN);
}
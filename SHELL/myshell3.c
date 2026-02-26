#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

/* ==================== 常量定义 ==================== */
#define LEFT "["
#define RIGHT "]"
#define DELIM  " \t"

#define LINE_SIZE 1024
#define ARGC_SIZE 64
#define EXIT_CODE 44
#define MAX_PIPELINE 16
#define MAX_JOBS 32

/* ==================== 颜色定义 ==================== */
#define NONE         "\033[m"
#define GREEN        "\033[1;32;32m"
#define BLUE         "\033[1;32;34m"
#define RED          "\033[1;31m"
#define YELLOW       "\033[1;33m"
#define CYAN         "\033[1;36m"

/* ==================== 结构体定义 ==================== */

// 重定向类型
typedef enum {
    REDIR_NONE,
    REDIR_INPUT,    // <
    REDIR_OUTPUT,   // >
    REDIR_APPEND    // >>
} RedirType;

// 重定向信息
typedef struct {
    RedirType type;
    char *file;
} RedirInfo;

// 命令结构
typedef struct {
    char **argv;           // 命令参数
    int argc;              // 参数个数
    RedirInfo input_redir;  // 输入重定向
    RedirInfo output_redir; // 输出重定向
    int background;         // 后台运行标志
    int pipe_next;          // 是否有管道下一个命令
} Command;

// 作业结构
typedef struct job {
    pid_t pgid;             // 进程组ID
    pid_t *pids;            // 所有进程ID
    int pid_count;          // 进程数量
    char *cmdline;          // 命令行
    int job_id;             // 作业ID
    int background;         // 是否后台
    int stopped;            // 是否停止
    int completed;          // 是否完成
    struct job *next;
} Job;

/* ==================== 全局变量 ==================== */
char commandline[LINE_SIZE];
char *argv[ARGC_SIZE];
char pwd[LINE_SIZE];
char *g_argv[ARGC_SIZE];    // 保存原始argv用于管道处理
int lastcode = 1;
Job *job_list = NULL;
int next_job_id = 1;
pid_t shell_pgid;
int shell_terminal;

/* ==================== 函数声明 ==================== */
const char *getusername();
const char *getmyhostname();
void getpwd();
void interact(char *cline, int size);
int splitstring(char *cline, char *Argv[]);
int parse_command_line(char *cline, Command *cmds[], int *cmd_count);
//void execute_command(Command *cmd, int cmd_count);
void execute_pipeline(Command *cmds[], int cmd_count);
void execute_simple(Command *cmd);
int handle_redirections(Command *cmd);
int is_builtin(Command *cmd);
int execute_builtin(Command *cmd);
void setup_signal_handlers();
void sigchld_handler(int sig);
void sigint_handler(int sig);
void add_job(pid_t pgid, pid_t *pids, int pid_count, char *cmdline, int background);
void remove_job(int job_id);
void update_job_status();
void put_job_foreground(Job *job, int cont);
void put_job_background(Job *job, int cont);
void free_command(Command *cmd);

/* ==================== 原有函数保持 ==================== */
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
    // 显示后台作业数量
    int bg_count = 0;
    Job *j = job_list;
    while (j) {
        if (j->background && !j->completed) bg_count++;
        j = j->next;
    }
    
    printf(GREEN LEFT"%s@%s"RIGHT NONE":" BLUE"%s"NONE, 
           getusername(), getmyhostname(), pwd);
    if (bg_count > 0) {
        printf(YELLOW"[%d]"NONE, bg_count);
    }
    printf("$ ");
    fflush(stdout);
    
    char *a = fgets(cline, size, stdin);
    assert(a != NULL);
    (void)a;
    cline[strlen(cline)-1] = '\0';
}

int splitstring(char *cline, char *Argv[]) {
    int i = 0;
    Argv[i++] = strtok(cline, DELIM);
    while((Argv[i++] = strtok(NULL, DELIM)));
    return i - 1;
}

/* ==================== 新增：命令行解析 ==================== */
int parse_command_line(char *cline, Command *cmds[], int *cmd_count) {
    char line_copy[LINE_SIZE];
    strcpy(line_copy, cline);
    
    // 检查后台运行符
    int background = 0;
    char *bg_pos = strchr(line_copy, '&');
    if (bg_pos) {
        background = 1;
        *bg_pos = '\0';  // 移除&
    }
    
    // 按管道分割
    char *pipe_saveptr;
    char *pipe_token = strtok_r(line_copy, "|", &pipe_saveptr);
    int count = 0;
    
    while (pipe_token && count < MAX_PIPELINE) {
        // 去除前后空格
        while (isspace(*pipe_token)) pipe_token++;
        char *end = pipe_token + strlen(pipe_token) - 1;
        while (end > pipe_token && isspace(*end)) end--;
        *(end + 1) = '\0';
        
        if (strlen(pipe_token) == 0) {
            fprintf(stderr, "语法错误：管道前后缺少命令\n");
            return -1;
        }
        
        // 创建命令结构
        Command *cmd = calloc(1, sizeof(Command));
        if (!cmd) {
            perror("calloc");
            return -1;
        }
        
        // 初始化重定向
        cmd->input_redir.type = REDIR_NONE;
        cmd->output_redir.type = REDIR_NONE;
        
        // 解析重定向
        char *redir_in = strchr(pipe_token, '<');
        char *redir_out = strchr(pipe_token, '>');
        
        // 处理输入重定向
        if (redir_in) {
            char *file = redir_in + 1;
            while (isspace(*file)) file++;
            
            // 提取文件名
            char *file_end = file;
            while (*file_end && !isspace(*file_end) && *file_end != '|' && *file_end != '>') 
                file_end++;
            char save = *file_end;
            *file_end = '\0';
            
            cmd->input_redir.type = REDIR_INPUT;
            cmd->input_redir.file = strdup(file);
            
            *file_end = save;
            
            // 从命令中移除重定向部分
            *redir_in = '\0';
        }
        
        // 处理输出重定向
        if (redir_out) {
            int append = 0;
            if (*(redir_out + 1) == '>') {
                append = 1;
                redir_out++;
            }
            
            char *file = redir_out + 1;
            while (isspace(*file)) file++;
            
            char *file_end = file;
            while (*file_end && !isspace(*file_end) && *file_end != '|' && *file_end != '<')
                file_end++;
            char save = *file_end;
            *file_end = '\0';
            
            cmd->output_redir.type = append ? REDIR_APPEND : REDIR_OUTPUT;
            cmd->output_redir.file = strdup(file);
            
            *file_end = save;
            *redir_out = '\0';
        }
        
        // 解析命令参数
        char *token_saveptr;
        char *token = strtok_r(pipe_token, DELIM, &token_saveptr);
        int argc = 0;
        char **argv_copy = malloc(sizeof(char *) * (ARGC_SIZE + 1));
        
        while (token && argc < ARGC_SIZE) {
            argv_copy[argc++] = strdup(token);
            token = strtok_r(NULL, DELIM, &token_saveptr);
        }
        argv_copy[argc] = NULL;
        
        cmd->argv = argv_copy;
        cmd->argc = argc;
        cmd->background = (count == 0) ? background : 0;  // 只有最后一个命令的background有效
        cmd->pipe_next = (pipe_token && *pipe_saveptr) ? 1 : 0;
        
        cmds[count++] = cmd;
        pipe_token = strtok_r(NULL, "|", &pipe_saveptr);
    }
    
    *cmd_count = count;
    return 0;
}

/* ==================== 新增：重定向处理 ==================== */
int handle_redirections(Command *cmd) {
    // 输入重定向
    if (cmd->input_redir.type == REDIR_INPUT) {
        int fd = open(cmd->input_redir.file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, RED"错误："NONE"无法打开输入文件 %s: %s\n", 
                    cmd->input_redir.file, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror(RED"dup2"NONE);
            close(fd);
            return -1;
        }
        close(fd);
    }
    
    // 输出重定向
    if (cmd->output_redir.type == REDIR_OUTPUT) {
        int fd = open(cmd->output_redir.file, 
                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, RED"错误："NONE"无法打开输出文件 %s: %s\n", 
                    cmd->output_redir.file, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror(RED"dup2"NONE);
            close(fd);
            return -1;
        }
        close(fd);
    }
    
    // 追加重定向
    if (cmd->output_redir.type == REDIR_APPEND) {
        int fd = open(cmd->output_redir.file, 
                     O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            fprintf(stderr, RED"错误："NONE"无法打开输出文件 %s: %s\n", 
                    cmd->output_redir.file, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror(RED"dup2"NONE);
            close(fd);
            return -1;
        }
        close(fd);
    }
    
    return 0;
}

/* ==================== 新增：内置命令 ==================== */
int is_builtin(Command *cmd) {
    if (!cmd || !cmd->argv || !cmd->argv[0]) return 0;
    char *name = cmd->argv[0];
    return (strcmp(name, "cd") == 0 ||
            strcmp(name, "exit") == 0 ||
            strcmp(name, "jobs") == 0 ||
            strcmp(name, "fg") == 0 ||
            strcmp(name, "bg") == 0 ||
            strcmp(name, "export") == 0 ||
            strcmp(name, "echo") == 0);
}

int execute_builtin(Command *cmd) {
    char *name = cmd->argv[0];
    
    if (strcmp(name, "cd") == 0) {
        char *path;
        char oldpwd[LINE_SIZE];
        getcwd(oldpwd, sizeof(oldpwd));
        
        if (cmd->argc == 1 || strcmp(cmd->argv[1], "~") == 0) {
            path = getenv("HOME");
            if (!path) path = "/";
        } else if (strcmp(cmd->argv[1], "-") == 0) {
            path = getenv("OLDPWD");
            if (!path) {
                fprintf(stderr, "cd: OLDPWD not set\n");
                return 1;
            }
            printf("%s\n", path);
        } else {
            path = cmd->argv[1];
        }
        
        if (chdir(path) < 0) {
            perror("cd");
            return 1;
        }
        
        setenv("OLDPWD", oldpwd, 1);
        getcwd(pwd, sizeof(pwd));
        setenv("PWD", pwd, 1);
        return 0;
    }
    else if (strcmp(name, "exit") == 0) {
        int status = 0;
        if (cmd->argc > 1) {
            status = atoi(cmd->argv[1]);
        }
        printf("exit\n");
        exit(status);
    }
    else if (strcmp(name, "jobs") == 0) {
        Job *j = job_list;
        while (j) {
            char status[16];
            if (j->completed) strcpy(status, "Done");
            else if (j->stopped) strcpy(status, "Stopped");
            else strcpy(status, "Running");
            
            printf("[%d] %s\t\t%s\n", j->job_id, status, j->cmdline);
            j = j->next;
        }
        return 0;
    }
    else if (strcmp(name, "fg") == 0) {
        Job *job = NULL;
        if (cmd->argc > 1) {
            int job_id = atoi(cmd->argv[1]);
            job = job_list;
            while (job) {
                if (job->job_id == job_id) break;
                job = job->next;
            }
        } else {
            // 获取最近的后台作业
            job = job_list;
        }
        
        if (job) {
            put_job_foreground(job, 1);
        } else {
            fprintf(stderr, "fg: 作业未找到\n");
        }
        return 0;
    }
    else if (strcmp(name, "bg") == 0) {
        Job *job = NULL;
        if (cmd->argc > 1) {
            int job_id = atoi(cmd->argv[1]);
            job = job_list;
            while (job) {
                if (job->job_id == job_id) break;
                job = job->next;
            }
        } else {
            job = job_list;
        }
        
        if (job && job->stopped) {
            put_job_background(job, 1);
        } else {
            fprintf(stderr, "bg: 作业未找到或已在运行\n");
        }
        return 0;
    }
    else if (strcmp(name, "export") == 0) {
        if (cmd->argc == 1) {
            extern char **environ;
            for (char **env = environ; *env; env++) {
                printf("%s\n", *env);
            }
        } else {
            for (int i = 1; i < cmd->argc; i++) {
                putenv(cmd->argv[i]);
            }
        }
        return 0;
    }
    else if (strcmp(name, "echo") == 0) {
        for (int i = 1; i < cmd->argc; i++) {
            if (cmd->argv[i][0] == '$') {
                char *val = getenv(cmd->argv[i] + 1);
                if (val) printf("%s ", val);
            } else {
                printf("%s ", cmd->argv[i]);
            }
        }
        printf("\n");
        return 0;
    }
    
    return 1;
}

/* ==================== 新增：管道执行 ==================== */
void execute_pipeline(Command *cmds[], int cmd_count) {
    int pipefds[2];
    int prev_read_fd = -1;
    pid_t pids[MAX_PIPELINE];
    int i;
    
    for (i = 0; i < cmd_count; i++) {
        // 创建管道（如果不是最后一个命令）
        if (i < cmd_count - 1) {
            if (pipe(pipefds) < 0) {
                perror(RED"pipe"NONE);
                return;
            }
        }
        
        pid_t pid = fork();
        
        if (pid < 0) {
            perror(RED"fork"NONE);
            return;
        }
        
        if (pid == 0) {
            // 子进程
            
            // 创建新进程组
            setpgid(0, 0);
            
            // 恢复默认信号处理
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            
            // 输入来自前一个命令
            if (prev_read_fd != -1) {
                dup2(prev_read_fd, STDIN_FILENO);
                close(prev_read_fd);
            }
            
            // 输出到下一个命令
            if (i < cmd_count - 1) {
                dup2(pipefds[1], STDOUT_FILENO);
                close(pipefds[1]);
                close(pipefds[0]);
            }
            
            // 处理文件重定向（会覆盖管道重定向）
            if (handle_redirections(cmds[i]) < 0) {
                exit(1);
            }
            
            // 执行命令
            execvp(cmds[i]->argv[0], cmds[i]->argv);
            
            // 如果exec返回，说明出错
            fprintf(stderr, RED"%s: 命令未找到"NONE"\n", cmds[i]->argv[0]);
            exit(127);
        }
        
        // 父进程
        pids[i] = pid;
        setpgid(pid, pid);
        
        // 关闭已使用的描述符
        if (prev_read_fd != -1) {
            close(prev_read_fd);
        }
        if (i < cmd_count - 1) {
            close(pipefds[1]);
            prev_read_fd = pipefds[0];
        }
    }
    
    // 确定进程组ID
    pid_t pgid = pids[0];
    
    // 构建命令行字符串用于显示
    char cmdline[LINE_SIZE] = "";
    for (i = 0; i < cmd_count; i++) {
        strcat(cmdline, cmds[i]->argv[0]);
        if (i < cmd_count - 1) strcat(cmdline, " | ");
    }
    
    // 检查是否为后台作业
    int background = cmds[cmd_count - 1]->background;
    
    if (!background) {
        // 前台作业
        add_job(pgid, pids, cmd_count, cmdline, 0);
        put_job_foreground(job_list, 0);
    } else {
        // 后台作业
        add_job(pgid, pids, cmd_count, cmdline, 1);
        printf("[%d] %d", next_job_id - 1, pgid);
        for (i = 1; i < cmd_count; i++) {
            printf(" %d", pids[i]);
        }
        printf("\n");
    }
}

/* ==================== 新增：简单命令执行 ==================== */
void execute_simple(Command *cmd) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror(RED"fork"NONE);
        return;
    }
    
    if (pid == 0) {
        // 子进程
        setpgid(0, 0);
        
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        
        // 处理重定向
        if (handle_redirections(cmd) < 0) {
            exit(1);
        }
        
        execvp(cmd->argv[0], cmd->argv);
        
        fprintf(stderr, RED"%s: 命令未找到"NONE"\n", cmd->argv[0]);
        exit(127);
    }
    
    // 父进程
    setpgid(pid, pid);
    
    if (!cmd->background) {
        // 前台命令
        add_job(pid, &pid, 1, cmd->argv[0], 0);
        put_job_foreground(job_list, 0);
    } else {
        // 后台命令
        add_job(pid, &pid, 1, cmd->argv[0], 1);
        printf("[%d] %d\n", next_job_id - 1, pid);
    }
}

/* ==================== 新增：命令执行入口 ==================== */
void execute_command(Command *cmds[], int cmd_count) {
    if (cmd_count == 0) return;
    
    // 检查是否为内置命令（只有单个命令且不是管道的一部分）
    if (cmd_count == 1 && is_builtin(cmds[0])) {
        execute_builtin(cmds[0]);
        return;
    }
    
    // 执行管道命令
    execute_pipeline(cmds, cmd_count);
}

/* ==================== 新增：信号处理 ==================== */
void setup_signal_handlers() {
    struct sigaction sa;
    
    // SIGINT (Ctrl+C) - shell忽略，子进程会继承默认处理
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    
    // SIGQUIT - 忽略
    sa.sa_handler = SIG_IGN;
    sigaction(SIGQUIT, &sa, NULL);
    
    // SIGTSTP (Ctrl+Z) - 交给作业控制
    sa.sa_handler = SIG_IGN;
    sigaction(SIGTSTP, &sa, NULL);
    
    // SIGCHLD - 处理子进程退出
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    
    // 忽略TTY信号
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        Job *job = job_list;
        while (job) {
            for (int i = 0; i < job->pid_count; i++) {
                if (job->pids[i] == pid) {
                    if (WIFSTOPPED(status)) {
                        job->stopped = 1;
                    } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                        // 检查是否所有进程都结束
                        int all_done = 1;
                        for (int j = 0; j < job->pid_count; j++) {
                            if (j != i && kill(job->pids[j], 0) == 0) {
                                all_done = 0;
                                break;
                            }
                        }
                        if (all_done) {
                            job->completed = 1;
                        }
                    }
                    break;
                }
            }
            job = job->next;
        }
    }
}

void sigint_handler(int sig) {
    // 重绘提示符
    printf("\n");
    // 清理输入缓冲区
    while (getchar() != '\n' && !feof(stdin));
    interact(commandline, sizeof(commandline));
    fflush(stdout);
}

/* ==================== 新增：作业管理 ==================== */
void add_job(pid_t pgid, pid_t *pids, int pid_count, char *cmdline, int background) {
    Job *job = malloc(sizeof(Job));
    if (!job) {
        perror(RED"malloc"NONE);
        return;
    }
    
    job->pgid = pgid;
    job->pids = malloc(sizeof(pid_t) * pid_count);
    if (!job->pids) {
        free(job);
        perror(RED"malloc"NONE);
        return;
    }
    
    memcpy(job->pids, pids, sizeof(pid_t) * pid_count);
    job->pid_count = pid_count;
    job->cmdline = strdup(cmdline);
    job->job_id = next_job_id++;
    job->background = background;
    job->stopped = 0;
    job->completed = 0;
    
    job->next = job_list;
    job_list = job;
}

void remove_job(int job_id) {
    Job *prev = NULL;
    Job *curr = job_list;
    
    while (curr) {
        if (curr->job_id == job_id) {
            if (prev) {
                prev->next = curr->next;
            } else {
                job_list = curr->next;
            }
            free(curr->pids);
            free(curr->cmdline);
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

void update_job_status() {
    Job *prev = NULL;
    Job *curr = job_list;
    
    while (curr) {
        if (curr->completed) {
            if (curr->background) {
                printf("\n[%d]+ Done\t\t%s\n", curr->job_id, curr->cmdline);
            }
            Job *next = curr->next;
            if (prev) {
                prev->next = next;
            } else {
                job_list = next;
            }
            free(curr->pids);
            free(curr->cmdline);
            free(curr);
            curr = next;
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}

void put_job_foreground(Job *job, int cont) {
    // 将作业放到前台
    tcsetpgrp(shell_terminal, job->pgid);
    
    if (cont) {
        // 继续作业
        kill(-job->pgid, SIGCONT);
        job->stopped = 0;
    }
    
    // 等待作业完成
    int status;
    for (int i = 0; i < job->pid_count; i++) {
        waitpid(job->pids[i], &status, WUNTRACED);
    }
    
    // 将shell放回前台
    tcsetpgrp(shell_terminal, shell_pgid);
    
    // 检查作业状态
    if (job->stopped) {
        printf("\n[%d]+ Stopped\t\t%s\n", job->job_id, job->cmdline);
    } else {
        remove_job(job->job_id);
    }
}

void put_job_background(Job *job, int cont) {
    if (cont) {
        kill(-job->pgid, SIGCONT);
        job->stopped = 0;
    }
}

/* ==================== 新增：命令释放 ==================== */
void free_command(Command *cmd) {
    if (!cmd) return;
    
    if (cmd->argv) {
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
        }
        free(cmd->argv);
    }
    
    if (cmd->input_redir.file) free(cmd->input_redir.file);
    if (cmd->output_redir.file) free(cmd->output_redir.file);
    
    free(cmd);
}

/* ==================== 主函数 ==================== */
int main() {
    extern char **environ;
    
    // 设置shell为前台进程组
    shell_terminal = STDIN_FILENO;
    shell_pgid = getpgrp();
    tcsetpgrp(shell_terminal, shell_pgid);
    
    // 设置信号处理
    setup_signal_handlers();
    
    int quit = 0;
    
    while (!quit) {
        // 检查作业状态
        update_job_status();
        
        // 交互输入
        interact(commandline, sizeof(commandline));
        
        if (strlen(commandline) == 0) {
            continue;
        }
        
        // 解析命令
        Command *cmds[MAX_PIPELINE];
        int cmd_count = 0;
        
        if (parse_command_line(commandline, cmds, &cmd_count) < 0) {
            continue;
        }
        
        if (cmd_count == 0) {
            continue;
        }
        
        // 执行命令
        execute_command(cmds, cmd_count);
        
        // 释放命令内存
        for (int i = 0; i < cmd_count; i++) {
            free_command(cmds[i]);
        }
    }
    
    return 0;
}
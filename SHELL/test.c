#include "myshell2.h"

int main() {
    getpwd();
    setup_signal_handlers();
    
    Command *commands[MAX_PIPELINE];
    int commandcount;
    
    while(1) {
        Interact(commandline, sizeof(commandline));
        
        if(strlen(commandline) == 0) {
            continue;
        }
        
        if(ParseCommand(commandline, commands, &commandcount) < 0) {
            fprintf(stderr, "解析命令失败\n");
            continue;
        }
        
        ExecuteCommand(commands, commandcount);

        for(int i = 0; i < commandcount; i++) {
            FreeCommand(commands[i]);
        }
    }
    
    return 0;
}
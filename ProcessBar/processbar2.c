#include "processbar.h"

const char *lable = "|/-\\";

char bar[NUM];

void processbar2(int rate) {
    
    if(rate < 0 || rate > 100) {
        return;
    }
    int len = strlen(lable);
    printf("[%-100s][%d%%][%c]\r", bar, rate, lable[rate%len]);
    fflush(stdout);
    bar[rate++] = BODY;
    if(rate < 100) {
        bar[rate] = RIGHT;
    }
    
    printf("\n");
}
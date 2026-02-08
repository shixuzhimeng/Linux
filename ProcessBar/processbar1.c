#include "processbar.h"

const char *lable = "|/-\\";

void processbar1(int speed) {
    char bar[NUM];
    memset(bar, '\0', sizeof(bar));
    int len = strlen(lable);

    int cnt = 0;
    while(cnt <= TOP) {
        printf("[%-100s][%d%%][%c]\r", bar, cnt, lable[cnt%len]);
        fflush(stdout);
        bar[cnt++] = BODY;
        if(cnt < 100) {
            bar[cnt] = RIGHT;
        }
        usleep(speed);
    }
    printf("\n");
}
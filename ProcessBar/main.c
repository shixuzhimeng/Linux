#include "processbar.h"

typedef void (*callback)(int);

void downlode(callback cb) {
    int total = 1000;
    int cur = 0;
    while(cur <= total) {
        usleep(20000);
        int rate = cur*100/total;

        cb(rate);//通过回调显示进度

        cur += 10;
    }
    printf("\n");
}

int main() {
    //processbar1(100000);

    downlode(processbar2);

    return 0;
}

// int main () {
//     int total = 1000;
//     int cur = 0;
//     while(cur <= total) {   
//         processbar2(cur*100/total);
//         cur += 10;
//         usleep(20000);
//     }
//     printf("\n");
// }
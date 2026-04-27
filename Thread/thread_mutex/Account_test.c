#include "account.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>

typedef struct {
    char name[20];
    Account *account;
    double amt;
}operArg;

void *withdraw_fn(void *arg) {
    operArg *oa = (operArg*)arg;
    double amt = withdraw(oa->account, oa->amt);

    printf("%8s(0x%lx) withdraw %f from account %d\n",
         oa->name, pthread_self(), amt, oa->account->code);
    
    return (void*)0;
}

void *deposit(void *arg) {
    operArg *oa = (operArg*)arg;
    double amt = desposit(oa->account, oa->amt);

    printf("%8s(0x%lx) deposit %f from account %d\n",
         oa->name, pthread_self(), amt, oa->account->code);
    
    return (void*)0;
}


int main() {
    int err;
    pthread_t boy, girl;
    Account *a = creat(100001, 10000);
    
    operArg o1, o2;
    
    strcpy(o1.name, "boy");
    o1.account = a;
    o1.amt = 10000;
    
    strcpy(o2.name, "girl");
    o2.account = a;
    o2.amt = 10000;


    //启动子线程
    if((err = pthread_create(&boy, NULL, withdraw_fn, (void*)&o1)) != 0) {
        perror("pthread_create failed");
    }
    if((err = pthread_create(&girl, NULL, withdraw_fn, (void*)&o2)) != 0) {
        perror("pthread_create failed");
    }

    pthread_join(boy, NULL);
    pthread_join(girl, NULL);

    printf("account balance: %f\n", get_balance(a));
    destory_account(a);
    //两个线程同时对一份数据进行修改，这时数据会产生问题
    return 0;
}
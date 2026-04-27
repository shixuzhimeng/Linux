#include "account.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>
#include <stdlib.h>

//创建账户
Account* creat(int code, double balance) {
    Account* new = (Account *)malloc(sizeof(Account));
    assert(new != NULL);
    new->code = code;
    new->balance  = balance;
    pthread_mutex_init(&new->mutex, NULL);
    return new;
}

//销毁账户
void destory_account(Account *a) {
    assert(a != 0);
    pthread_mutex_destroy(&a->mutex);
    free(a);
}

//取款
double withdraw(Account *a, double amt) {
    assert(a != NULL);
    pthread_mutex_lock(&a->mutex);
    // ====   临界区   ====
    if(amt == 0 || amt > a->balance) {
        pthread_mutex_unlock(&a->mutex);
        return 0.0;
    }

    double balance = a->balance;
    sleep(1);
    balance -= amt;
    a->balance = balance;
    // ===================
    pthread_mutex_unlock(&a->mutex);
    return amt;
}
//存款
double  desposit(Account *a, double amt) {
    assert(a != 0);
    pthread_mutex_lock(&a->mutex);
    if(amt < 0) {
        pthread_mutex_unlock(&a->mutex);
        return 0.0;
    }
    double balance = a->balance;
    balance += amt;
    a->balance = balance;
    pthread_mutex_unlock(&a->mutex);
    return amt;
}

//查看账户余额
double get_balance(Account *a) {
    assert(a != NULL);
    double balance = a->balance;
    return balance;
}

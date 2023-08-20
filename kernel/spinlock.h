#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_
#include "types.h"
struct spinlock {
    uint64 locked;
    char* name;
    uint64 cpu;
};
void initlock(struct spinlock* lk, char* name);
void acquire(struct spinlock* lk);
void release(struct spinlock* lk);
int holding(struct spinlock* lk);
void push_off();
void pop_off();
#endif

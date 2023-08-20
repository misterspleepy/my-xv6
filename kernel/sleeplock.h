#ifndef _SLEEPLOCK_H_
#define _SLEEPLOCK_H_
#include "spinlock.h"
struct sleeplock {
    struct spinlock lock;
    uint locked;
    char* name;
    int pid;
};
void initsleeplock(struct sleeplock* lk, char* name);
int holdingsleep(struct sleeplock* lk);
void acquiresleep(struct sleeplock* lk);
void releasesleep(struct sleeplock* lk);
#endif
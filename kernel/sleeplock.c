#include "sleeplock.h"
#include "spinlock.h"
#include "proc.h"
#include "utils.h"
void initsleeplock(struct sleeplock* lk, char* name)
{
    initlock(&lk->lock, name);
    lk->locked = 0;
    lk->name = name;
    lk->pid = 0; 
    
}

int holdingsleep(struct sleeplock* lk)
{
    return lk->locked && (lk->pid == myproc()->pid);
}

void acquiresleep(struct sleeplock* lk)
{
    while (1) {
        acquire(&lk->lock);
        if (!lk->locked) {
            lk->locked = 1;
            lk->pid = myproc()->pid;
            release(&lk->lock);
            break;
        }
        sleep(lk, &lk->lock);
    }
}

void releasesleep(struct sleeplock* lk)
{
    if (!holdingsleep(lk)) {
        panic("releasesleep\n");
    }
    acquire(&lk->lock);
    lk->locked = 0;
    lk->pid = 0;
    release(&lk->lock);
    wakeup(lk);
}
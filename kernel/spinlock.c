#include "riscv.h"
#include "proc.h"
#include "spinlock.h"
#include "types.h"
#include "utils.h"
void initlock(struct spinlock* lk, char* name)
{
    lk->locked = 0;
    lk->name = name;
    lk->cpu = 0;
}

void acquire(struct spinlock* lk)
{
    push_off();
    while(__sync_lock_test_and_set(&lk->locked, 1) != 0) {
        ;
    }
    __sync_synchronize();
    lk->cpu = (uint64)mycpu();
}

void release(struct spinlock* lk)
{
    if (!holding(lk)) {
        panic("release");
    }
    lk->cpu = 0;
    __sync_synchronize();
    __sync_lock_release(&lk->locked);
    pop_off();
}

int holding(struct spinlock* lk)
{
    int r;
    r = (lk->locked && lk->cpu == (uint64)mycpu());
    return r;
}

void push_off()
{
    int old = intr_get();
    intr_off();
    if (mycpu()->noff == 0) {
        mycpu()->intena = old;
    } 
    mycpu()->noff += 1;
}

void pop_off()
{
    struct cpu* cpu = mycpu();
    if (intr_get() || cpu->noff < 1) {
        panic("pop off");
    }
    cpu->noff -= 1;
    if (cpu->noff == 0 && cpu->intena) {
        intr_on();
    }
}
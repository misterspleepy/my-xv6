#include "riscv.h"
#include "memlayout.h"
#include "utils.h"
extern char end[];
struct run {
    struct run* next;
};

struct {
    struct run head;
} klist;

void freerange(void* pa_start, void* pa_end)
{   
    for (uint64 i = (uint64)PGROUNDUP((uint64)pa_start); i + PGSIZE <= (uint64)pa_end; i += PGSIZE) {
        struct run* p = (struct run*)i;
        p->next = klist.head.next;
        klist.head.next = p;
    }
}

void kinit()
{
    klist.head.next = 0;
    freerange((void*)end, (void*)PHYSTOP);
}

void* kalloc()
{
    struct run* free = klist.head.next;
    if (!free) {
        return 0;
    }
    klist.head.next = free->next;
    return (void*)free;
}

void kfree(void* pa)
{
    if ((uint64)pa % PGSIZE) {
        panic("kfree's argument must 4k align\n");
    }
    struct run* free = (struct run*)pa;
    free->next = klist.head.next;
    klist.head.next = free;
}
#include "param.h"
#include "proc.h"
#include "vm.h"
#include "memlayout.h"
#include "utils.h"
#include "kmem.h"
#include "types.h"
#include "string.h"
struct proc procs[N_PROC];
struct cpu cpus[N_CPU];
extern char trampoline[];

struct cpu* mycpu()
{
    return &cpus[cpuid()];
}

struct proc* myproc()
{
    return mycpu()->proc;
}

uint64 allocpid()
{
    static uint64 next_pid = 0;
    return next_pid++;
}

void procinit()
{
    for (int i = 0; i < N_PROC; i++) {
        procs[i].status = UNUSED;
    }
}

void scheduler()
{
    int i = 0;
    while(1) {
        i++;
        
        i %= N_PROC;
        if (procs[i].status != RUNNABLE) {
            continue;
        }
        procs[i].status = RUNNING;
        mycpu()->proc = &procs[i];
        swtch(&cpus[cpuid()].con, &procs[i].context);
        myproc()->status = RUNNABLE;
        mycpu()->proc = 0;
    }
}

unsigned char initcode[] = {0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02, 0x93, 0x08, 0x00, 0x00, 0x73, 0x00, 0x00, 0x00
, 0xb7, 0x74, 0xc2, 0x48, 0x9b, 0x84, 0x54, 0x39, 0x93, 0x94, 0xd4, 0x00, 0x63, 0x10, 0x90, 0x00
, 0xef, 0xf0, 0x1f, 0xfe, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x75, 0x73, 0x65, 0x72, 0x0a, 0x00
, 0x00, 0x00, 0x00, 0x00
};

void usertrapret();
void forkret()
{
    usertrapret();
}

pagetable_t proc_pagetable(struct proc* proc)
{
    // alloc a empty page
    pagetable_t ptl = (pagetable_t)kalloc();
    memset((char*)ptl, 0, PGSIZE);
    // map TRANPOLINE and TRAPFRAME
    mappages(ptl, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X);
    mappages(ptl, TRAPFRAME, PGSIZE, (uint64)proc->trapframe, PTE_R | PTE_W | PTE_X);
    return ptl;
}

struct proc* allocproc()
{
    struct proc* p = 0;
    for (int i = 0; i < N_PROC; i++) {
        p = &procs[i];
        if (p->status == UNUSED) {
            goto FOUND;
        }
    }
    return 0;
FOUND:
    p->status = USED;
    p->pid = allocpid();
    p->kstack = KSTACK(p - procs);
    p->sz = 0;
    p->trapframe = kalloc();
    memset((char*)p->trapframe, 0, PGSIZE);

    // An empty user page table
    p->pagetable = proc_pagetable(p);
    memset((char*)&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;
    return p;
}

void uvmfirst(pagetable_t pgtbl, unsigned char* initcode, uint64 sz)
{
    if (sz > PGSIZE) {
        panic("uvmfirst");
    }
    char* freepage =  (char*)kalloc();
    memset(freepage, 0, PGSIZE);
    memmove(freepage, (char*)initcode, sz);
    mappages(pgtbl, 0, PGSIZE, (uint64)freepage, PTE_R | PTE_X | PTE_W | PTE_U);
}

void userinit()
{
    struct proc* p;
    p = allocproc();
    uvmfirst(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    p->trapframe->epc = 0;
    p->trapframe->sp = PGSIZE;
    p->status = RUNNABLE;
}

void yield()
{
    myproc()->status = RUNNABLE;
    swtch(&myproc()->context, &mycpu()->con);
}

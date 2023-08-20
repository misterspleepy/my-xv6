#include "param.h"
#include "proc.h"
#include "vm.h"
#include "memlayout.h"
#include "utils.h"
#include "kmem.h"
#include "types.h"
#include "string.h"
#include "stat.h"
#include "defs.h"

struct proc procs[N_PROC];
struct cpu cpus[N_CPU];
struct proc *initproc;
extern char trampoline[];

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

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
        initlock(&procs[i].lock, "proc");
    }
}

void scheduler()
{
    mycpu()->proc = 0;
    while(1) {
        intr_on();
        for (int i = 0; i < N_PROC; i++) {
            acquire(&procs[i].lock);
            if (procs[i].status != RUNNABLE) {
                release(&procs[i].lock);
                continue;
            }
            procs[i].status = RUNNING;
            mycpu()->proc = &procs[i];
            int intena = mycpu()->intena;
            int noff = mycpu()->noff;
            swtch(&cpus[cpuid()].con, &procs[i].context);
            mycpu()->noff = noff;
            mycpu()->intena = intena;
            mycpu()->proc = 0;
            release(&procs[i].lock);
        }
    }
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

void usertrapret();
static int first = 1;
void forkret()
{
    release(&myproc()->lock);
    if (first) {
        first = 0;
        fsinit(ROOTDEV);
    }
    usertrapret();
}

pagetable_t proc_pagetable(struct proc* proc)
{
    // alloc a empty page
    pagetable_t ptl = (pagetable_t)kalloc();
    memset((char*)ptl, 0, PGSIZE);
    // map TRANPOLINE and TRAPFRAME
    if (mappages(ptl, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X)) {
        kfree((void*)ptl);
        return 0;
    }
    if (mappages(ptl, TRAPFRAME, PGSIZE, (uint64)proc->trapframe, PTE_R | PTE_W | PTE_X)) {
        uvmunmap(ptl, TRAMPOLINE, 1, 0);
        kfree((void*)ptl);
        return 0;
    }
    return ptl;
}

void proc_freepagetable(pagetable_t pgtl, uint64 sz)
{
    uvmunmap(pgtl, TRAMPOLINE, 1, 0);
    uvmunmap(pgtl, TRAPFRAME, 1, 0);
    uvmfree(pgtl, sz);
}

int growproc(int n)
{
    struct proc* p;
    int newsz;
    if (n == 0) {
        return 0;
    }
    p = myproc();
    if (n > 0) {
        newsz = uvmalloc(p->pagetable, p->sz, p->sz + n, PTE_W | PTE_R);
        if (newsz == 0) {
            return -1;
        }
    } else {
        newsz = uvmdealloc(p->pagetable, p->sz, p->sz + n);
    }
    p->sz = newsz;
    return 0;
}
void freeproc(struct proc* p);
struct proc* allocproc()
{
    struct proc* p = 0;
    void* frame;
    for (int i = 0; i < N_PROC; i++) {
        p = &procs[i];
        acquire(&p->lock);
        if (p->status == UNUSED) {
            goto FOUND;
        } else {
            release(&p->lock);
        }
    }
    return 0;
FOUND:
    frame = kalloc();
    if (!frame) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }
    p->status = USED;
    p->pid = allocpid();
    p->kstack = KSTACK(p - procs);
    p->sz = 0;
    p->trapframe = frame;
    memset((char*)p->trapframe, 0, PGSIZE);

    // An empty user page table
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }
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
    initproc = p;
    if (p == 0) {
        panic("userinit\n");
    }
    uvmfirst(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    p->trapframe->epc = 0;
    p->trapframe->sp = PGSIZE;
    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");
    p->status = RUNNABLE;
    release(&p->lock);
}

int fork()
{
    int i, pid;
    struct proc* np;
    struct proc* p = myproc();
    
    // Allocate process.
    if ((np = allocproc()) == 0) {
        return -1;
    }

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // Copy saved user register
    *(np->trapframe) = *(p->trapframe);

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

    // increment reference counts on open file descriptors
    for (i =0; i < NOFILE; i++) {
        if (p->ofile[i]) {
            np->ofile[i] = filedup(p->ofile[i]);
        }
    }
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));
    pid = np->pid;
    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->status = RUNNABLE;
    release(&np->lock);

    return pid;
}

void sched()
{
    struct proc* p = myproc();
    if (p->status == RUNNING) {
        panic("sched\n");
    }
    int noff = mycpu()->noff;
    int intena = mycpu()->intena;
    swtch(&myproc()->context, &mycpu()->con);
    mycpu()->intena = intena;
    mycpu()->noff = noff;
}

void yield()
{
    acquire(&myproc()->lock);
    myproc()->status = RUNNABLE;
    sched();
    release(&myproc()->lock);
}

void freeproc(struct proc* p)
{
    if (p->trapframe) {
        kfree(p->trapframe);
    }
    if (p->pagetable) {
        proc_freepagetable(p->pagetable, p->sz);
    }
    p->status = UNUSED;
    p->xstatus = 0;
    p->sz = 0;
    p->pid = 0;
    p->pagetable = 0;
    p->trapframe = 0;
    p->killed = 0;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = procs; pp < &procs[N_PROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

void exit(int xstatus)
{
    // free proc
    struct proc* p = myproc();
    if(p == initproc) {
        panic("init exiting");
    }

    // Close all open files.
    for(int fd = 0; fd < NOFILE; fd++){
        if(p->ofile[fd]){
        struct file *f = p->ofile[fd];
        fileclose(f);
        p->ofile[fd] = 0;
        }
    }
    iput(p->cwd);
    p->cwd = 0;

    acquire(&wait_lock);
    // Give any children to init.
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup(p->parent);

    acquire(&p->lock);
    p->status = ZOMBIE;
    p->xstatus = xstatus;
    release(&wait_lock);
    sched();
    panic("zombie exit");
}

int setkilled(struct proc* p)
{
    p->killed = 1;
    return 0;
}

int killed(struct proc* p)
{
    return p->killed;
}

int kill(uint64 pid)
{
    struct proc* p;
    for (int i = 0; i < N_PROC; i++) {
        p = &procs[i];
        if (p->pid == pid) {
            // wakeup 之后应该立马判断是否被killed，是则退出进程
            // killed 的判断时机，进程中断函数
            acquire(&p->lock);
            if (p->status == SLEEPING) {
                p->status = RUNNABLE;
            }
            p->killed = 1;
            release(&p->lock);
            return 0;
        }
    }
    return -1;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = procs; pp < &procs[N_PROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->status == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstatus,
                                  sizeof(pp->xstatus)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

void sleep(void* chan, struct spinlock* lk)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->chan = chan;
    p->status = SLEEPING;
    if (lk && !holding(lk)) {
        panic("sleep\n");
    }
    if (lk) {
        release(lk);
    }

    sched();
    if (lk) {
        acquire(lk);
    }
    p->chan = 0;
    release(&p->lock);
}

void wakeup(void* chan)
{
    struct proc *p;
    for (p = procs; p < &procs[N_PROC]; p++) {
        if (p != myproc()) {
            acquire(&p->lock);
            if (p->status == SLEEPING && p->chan == chan) {
                p->status = RUNNABLE;
            }
            release(&p->lock);
        } 
    }
}

int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

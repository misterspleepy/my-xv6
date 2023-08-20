#include "spinlock.h"
#include "param.h" 
#include "riscv.h"
#include "file.h"
#include "defs.h"
#include "proc.h"
#define PIPESIZE 512
struct pipe {
    struct spinlock lock;
    uint readable;
    uint writable;
    uint rd;
    uint wt;
    uchar buf[512];
};

int pipealloc(struct file **f0, struct file **f1)
{
    struct pipe *pi;
    pi = 0;
    *f0 = *f1 = 0;
    if ((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0) {
        goto bad;
    }
    if ((pi = (struct pipe*)kalloc()) == 0) {
        goto bad;
    }
    pi->readable = 1;
    pi->writable = 1;
    pi->rd = 0;
    pi->wt = 0;
    initlock(&pi->lock, "pipe");
    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = pi;
    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = pi;
    return 0;
bad:
    if(pi) {
        kfree((char*)pi);
    }
    if(*f0) {
        fileclose(*f0);
    }
    if(*f1) {
        fileclose(*f1);
    }
    return -1;

}

void pipeclose(struct pipe* pi, int writable)
{
    acquire(&pi->lock);
    if (writable) {
        pi->writable = 0;
        wakeup(&pi->rd);
    } else {
        pi->readable = 0;
        wakeup(&pi->wt);
    }
    if (pi->readable == 0 && pi->writable == 0) {
        release(&pi->lock);
        kfree((char*)pi);
        return;
    }
    release(&pi->lock);
}

int piperead(struct pipe* pi, uint64 va, int n)
{
  int i;
  struct proc *pr = myproc();
  char ch;

  acquire(&pi->lock);
  while(pi->rd == pi->wt && pi->writable){  //DOC: pipe-empty
    if(killed(pr)){
      release(&pi->lock);
      return -1;
    }
    sleep(&pi->rd, &pi->lock); //DOC: piperead-sleep
  }
  for(i = 0; i < n; i++){  //DOC: piperead-copy
    if(pi->rd == pi->wt)
      break;
    ch = pi->buf[pi->rd++ % PIPESIZE];
    if(copyout(pr->pagetable, va + i, &ch, 1) == -1)
      break;
  }
  wakeup(&pi->wt);  //DOC: piperead-wakeup
  release(&pi->lock);
  return i;
}

int pipewrite(struct pipe* pi, uint64 va, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while(i < n){
    if(pi->readable == 0 || killed(pr)){
      release(&pi->lock);
      return -1;
    }
    if(pi->wt == pi->rd + PIPESIZE){ //DOC: pipewrite-full
      wakeup(&pi->rd);
      sleep(&pi->wt, &pi->lock);
    } else {
      char ch;
      if(copyin(pr->pagetable, &ch, va + i, 1) == -1)
        break;
      pi->buf[pi->wt++ % PIPESIZE] = ch;
      i++;
    }
  }
  wakeup(&pi->rd);
  release(&pi->lock);

  return i;
}
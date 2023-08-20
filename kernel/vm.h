#ifndef _VM_H_
#define _VM_H_
#include "riscv.h"
#include "types.h"
int mappages(pagetable_t pagetable, uint64 va, uint64 sz, uint64 pa, int perm);
void kvminit();
int copyin(pagetable_t pagetable, char* dst, uint64 srcva, uint64 len);
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int dofree);
void uvmfree(pagetable_t pagetable, uint64 sz);
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz);
void pagetabledump(pagetable_t pt, int level);
#endif
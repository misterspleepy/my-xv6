#ifndef _VM_H_
#define _VM_H_
#include "riscv.h"
int mappages(pagetable_t pagetable, uint64 va, uint64 sz, uint64 pa, int perm);
void kvminit();
int copyin(pagetable_t pagetable, char* dst, uint64 srcva, uint64 len);
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);
#endif
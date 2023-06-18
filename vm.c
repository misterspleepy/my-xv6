#include "vm.h"
#include "utils.h"
#include "param.h"
#include "memlayout.h"
#include "kmem.h"
#include "string.h"
extern char etext[];
extern char erodata[];
extern char edata[];
extern char trampoline[];
pagetable_t kernel_pagetable;
pte_t* walk(pagetable_t pagetable, uint64 va, int alloc)
{
    pte_t* pte = 0;
    for (int i = 2; i > 0; i--) {
        pte = (pte_t*)(pagetable + PX(i, va));
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PTE2PA(*pte);
            continue;
        }
        if (alloc == 0) {
            return 0;
        }
        pagetable = (pagetable_t)kalloc();
        if (!pagetable){
            return 0;
        }
        memset((char*)pagetable, 0, PGSIZE);
        *pte = PA2PTE(pagetable) | PTE_V;
    }
    return (pte_t*)(pagetable + PX(0, va));
}

// 如果不存在返回0
uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t* pte = walk(pagetable, va, 0);
    if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) {
        return 0;
    }
    return PTE2PA(*pte); 
}

int mappages(pagetable_t pagetable, uint64 va, uint64 sz, uint64 pa, int perm)
{
    uint64 v = 0;
    for (v = PGROUNDDOWN(va); v + PGSIZE <= PGROUNDUP(va + sz - 1); v += PGSIZE) {
        pte_t* pte = walk(pagetable, v, 1);
        if (!pte) {
            return -1;
        }
        if (*pte & PTE_V) {
            panic("mappages:remap");
        }
        *pte = PA2PTE(PGROUNDDOWN(pa) + v - PGROUNDDOWN(va)) | perm | PTE_V;
    }
    return 0;
}

int copyin(pagetable_t pagetable, char* dst, uint64 srcva, uint64 len)
{
    uint64 n, va0, pa0;
    while (len > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) {
            return -1;
        }
        n = PGSIZE - (srcva - va0);
        if (n > len) {
            n = len;
        }
        memmove(dst, (void*)(pa0 + srcva - va0), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
    uint64 n, va0, pa0;
    int got_null = 0;
    while (got_null == 0 && max > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) {
            return -1;
        }
        n = PGSIZE - (srcva - va0);
        if (n > max) {
            n = max;
        }
        char* p = (char*) (pa0 + (srcva - va0));
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            } else {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }
        srcva = va0 + PGSIZE;
    }
    if (got_null) {
        return 0;
    } else {
        return -1;
    }
}

void proc_mapstacks(pagetable_t kpgtbl)
{
    // alloc a empty page , and map to 
    for (int i = 0; i < N_PROC; i++) {
        uint64 stack = (uint64)kalloc();
        memset((char*)stack,0, PGSIZE);
        mappages(kpgtbl, KSTACK(i), PGSIZE, stack, PTE_R | PTE_W);
    }
}

pagetable_t kvmmake()
{
    // uart registers
    mappages(kernel_pagetable, UART0,  PGSIZE, UART0, PTE_R | PTE_W);

    mappages(kernel_pagetable, KERNBASE, (uint64)etext - (uint64)KERNBASE, KERNBASE,PTE_R|PTE_X);
    mappages(kernel_pagetable, (uint64)etext, (uint64)PHYSTOP -(uint64)etext, (uint64)etext, PTE_R | PTE_W | PTE_X| PTE_V);
    mappages(kernel_pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X);
    proc_mapstacks(kernel_pagetable);
    return kernel_pagetable;
}

void kvminit()
{
    kernel_pagetable = (pagetable_t)kalloc();
    memset((char*)kernel_pagetable, 0, PGSIZE);
    kvmmake();
    sfence_vma();
    w_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

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
        pte = (pte_t*)(pagetable) + PX(i, va);
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
    return (pte_t*)(pagetable) + PX(0, va);
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

void freewalk(pagetable_t pagetable)
{
    uint64 child;
    pte_t* pte;
    for (int i = 0; i < PGSIZE / sizeof(pte_t); i++) {
        pte = (pte_t*)pagetable + i;
        if (*pte & PTE_V && ((*pte & (PTE_R | PTE_W | PTE_X)) == 0)) {
            child = PTE2PA(*pte);
            freewalk((pagetable_t)child);
        } else if (*pte & PTE_V) { // leaf
            panic("freewalk\n");
        }
    }
    kfree((void*)pagetable);
}

void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int dofree)
{
    pte_t* pte = 0;
    uint64 pa = 0;
    if (va % PGSIZE) {
        panic("uvmunmap\n");
    }
    while (npages--) {
        pte = walk(pagetable, va, 0);
        if (!pte) {
            panic("uvmunmap: pte");
        }
        if ((*pte & PTE_V) == 0) { // not mapped
            panic("uvmunmap: not mapped");
        }
        if (PTE_FLAGS(*pte) == PTE_V) { // not leaf
            panic("uvmunmap: not leaf");
        }
        if (dofree) {
            pa = PTE2PA(*pte);
            kfree((void*)pa);
        }
        *pte = 0;
        va += PGSIZE;
    }
}

void uvmfree(pagetable_t pagetable, uint64 sz)
{
    if (sz > 0) {
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    }
    freewalk(pagetable);
}

uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
    uint64 pa;
    if (PGROUNDUP(oldsz) == PGROUNDUP(newsz)) {
        return oldsz;
    }
    oldsz = PGROUNDUP(oldsz);
    uint64 va = oldsz;
    for (; va < PGROUNDUP(newsz); va += PGSIZE) {
        pa = (uint64)kalloc();
        if (!pa) {
            break;
        }
        if (mappages(pagetable, va, PGSIZE, pa, PTE_R | PTE_U | xperm)) {
            kfree((void*)pa);
            break;
        }
        memset((void*)pa, 0, PGSIZE);
    }
    return va;
}

uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    if (oldsz <= newsz) {
        return oldsz;
    }
    uvmunmap(pagetable, PGROUNDUP(newsz), (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE, 1);
    return newsz;
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

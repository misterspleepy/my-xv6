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

void pagetabledump(pagetable_t pt, int level)
{
    char buf[10];
    uint64 child;
    pte_t* pte;
    int i = 0;
    for (; i < level * 4; i++) {
        buf[i] = ' ';
    }
    buf[i] = 0;
    printf("%s%p\n", buf, pt);
    for (int j = 0; j < PGSIZE / sizeof(pte_t); j++) {
        pte = (pte_t*)pt + j;
        if (*pte & PTE_V && ((*pte & (PTE_R | PTE_W | PTE_X)) == 0)) {
            child = PTE2PA(*pte);
            pagetabledump((pagetable_t)child, level + 1);
        } else if (*pte & PTE_V) { // leaf
            printf("    %sleaf:%p\n", buf, PTE2PA(*pte));
        }
    }
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

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    pte_t* pte;
    uint64 pa, i;
    uint flags;
    char* mem;

    for (i = 0; i < sz; i+= PGSIZE) {
        if ((pte = walk(old, i, 0)) == 0) {
            panic("uvmcopy:pte should exist");
        }
        if ((*pte & PTE_V) == 0) {
            panic("uvmcopy: page not present");
        }
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        if ((mem = kalloc()) == 0) {
            goto err;
        }
        memmove(mem, (char*)pa, PGSIZE);
        if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
            kfree(mem);
            goto err;
        }
    }
    return 0;
err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
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

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
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

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
    uint64 n, va0, pa0;

    while(len > 0){
        va0 = PGROUNDDOWN(dstva);
        pa0 = walkaddr(pagetable, va0);
        if(pa0 == 0) {
            return -1;
        }
        n = PGSIZE - (dstva - va0);
        if(n > len) {
            n = len;
        }   
        memmove((void *)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
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

    // virtio mmio disk interface
    mappages(kernel_pagetable, VIRTIO0, PGSIZE, VIRTIO0, PTE_R | PTE_W);

    // PLIC
    mappages(kernel_pagetable, PLIC, 0x400000, PLIC, PTE_R | PTE_W);

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

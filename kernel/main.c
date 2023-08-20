#include "riscv.h"
#include "proc.h"
#include "param.h"
#include "kmem.h"
#include "vm.h"

void kernelvec();
void binit(void);
void virtio_disk_init(void);
void plicinit(void);
void plicinithart(void);
void consoleinit(void);
void main()
{
    kinit();
    kvminit(); // switch to kernelpagetable
    procinit();
    w_stvec((uint64)kernelvec);
    binit();
    plicinit();
    plicinithart();
    virtio_disk_init();
    consoleinit();
    userinit();
    scheduler();
    while(1);
}
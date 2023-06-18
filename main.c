#include "riscv.h"
#include "proc.h"
#include "param.h"
#include "kmem.h"
#include "vm.h"

void kernelvec();
void main()
{
    kinit();
    kvminit(); // switch to kernelpagetable
    procinit();
    userinit();
    scheduler();
    while(1);
}
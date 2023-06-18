#include "types.h"
#include "proc.h"
#include "utils.h"
#include "memlayout.h"
void usertrapret();
extern char _trampoline[];
extern char trampoline[];
void userret(uint64 satp);
void uservec();
void usertrapret();
void syscall();
void yield();
void usertrap()
{
    // 根据情况决定是否+4 sepc
    // 根据trap的类型做操作
    // 如果是timer，就应该give up
    // 如果是系统调用， 那就调用系统调用
    // 如果是device中断，就处理device
    uint64 scause = r_scause();
    if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
        printf("supervisor external interrupt\n");
    } 
    if (scause == 0x8000000000000001L) {
        printf("software interrupt from mtimer\n");
        w_sip(r_sip() & ~2);
        yield();
    }
    if (scause == 0) {
        panic("Instruction address misaligned\n");
    }
    if (scause == 1) {
        panic("Instruction access fault\n");
    }
    if (scause == 2) {
        panic("Illegal instruction\n");
    }
    if (scause == 3) {
        printf("Breakpoint\n");
    }
    if (scause == 5) {
        printf("Load access fault\n");
    }
    if (scause == 6) {
        printf("Store/AMO address misaligned\n");
    }
    if (scause == 6) {
        printf("Store/AMO address misaligned\n");
    }
    if (scause == 7) {
        printf("Store/AMO access fault\n");
    }
    if (scause == 12) {
        printf("Instruction page fault\n");
        return;
    }
    if (scause == 13) {
        printf("Load page fault\n");
        return;
    }
    if (scause == 15) {
        printf("Store/AMO Page Fault\n");
        return;
    }
    if(r_scause() == 8) {
        myproc()->trapframe->epc = r_sepc() + 4;
        syscall();
    }
    usertrapret();
}

void usertrapret()
{
    w_stvec((uint64)((char*)uservec - _trampoline) + TRAMPOLINE);
    w_sstatus((r_sstatus() | SSTATUS_SPIE) & ~SSTATUS_SPP);
    myproc()->trapframe->kernel_trap = (uint64)usertrap;
    myproc()->trapframe->kernel_satp = (uint64)r_satp();
    myproc()->trapframe->kernel_sp  = (uint64)(myproc()->kstack) + PGSIZE;
    myproc()->trapframe->kernel_hartid = r_tp();
    uint64 trampoline_userret = TRAMPOLINE + ((char*)userret - trampoline);
    ((void (*)(uint64))trampoline_userret)(MAKE_SATP(myproc()->pagetable));
}

void kerneltrap()
{
    printf("kernel trap\n");
}
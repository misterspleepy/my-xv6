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
void kernelvec();
int plic_claim(void);
void plic_complete(int irq);
void virtio_disk_intr();
void uartintr(void);
uint ticks;

char EXCEPTION_CAUSE[][100] = {
[0] "Instruction address misaligned\n",
[1] "Instruction access fault\n",
[2] "Illegal instruction\n",
[3] "Breakpoint\n",
[4] "Load address misaligned\n",
[5] "Load access fault\n",
[6] "Store/AMO address misaligned\n",
[7] "Store/AMO access fault\n",
[8] "Environment call from U-mode\n",
[9] "Environment call from S-mode\n",
[10] "Reserved\n",
[11] "Environment call from M-mode\n",
[12] "Instruction page fault\n",
[13] "Load page fault\n",
[14] "Reserved\n",
[15] "Store/AMO page fault\n"
};
void clockintr()
{
    ticks++;
    wakeup(&ticks);
}

int devintr()
{
    uint64 scause = r_scause();
    if((scause & 0x8000000000000000L) &&
        (scause & 0xff) == 9) {
        // irq indicates which device interrupted.
        int irq = plic_claim();

        if(irq == UART0_IRQ){
            uartintr();
        } else if(irq == VIRTIO0_IRQ){
            virtio_disk_intr();
        } else if(irq){
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // the PLIC allows each device to raise at most one
        // interrupt at a time; tell the PLIC the device is
        // now allowed to interrupt again.
        if(irq)
            plic_complete(irq);
        return 1;
    }
    if (scause == 0x8000000000000001L) {
        if (cpuid() == 0) {
            clockintr();
        }
        w_sip(r_sip() & ~2);
        return 2;
    } else {
        printf("%s", EXCEPTION_CAUSE[scause]);
        return 0;
    }
}

void usertrap()
{
    // 根据情况决定是否+4 sepc
    // 根据trap的类型做操作
    // 如果是timer，就应该give up
    // 如果是系统调用， 那就调用系统调用
    // 如果是device中断，就处理device
    // save user program counter.
    int which_dev = 0;
    w_stvec((uint64)kernelvec);
    struct proc *p = myproc();
    p->trapframe->epc = r_sepc();

    uint64 scause = r_scause();
    if (scause == 8) {
        if (killed(p)) {
            exit(-1);
        }
        p->trapframe->epc += 4;
        intr_on();
        syscall();
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        setkilled(p);
    }

    if (killed(p)) {
        exit(-1);
    }

    if (which_dev == 2) {
        yield();
    }
    usertrapret();
}

void usertrapret()
{
    struct proc* p = myproc();
    intr_off();
    w_stvec((uint64)((char*)uservec - _trampoline) + TRAMPOLINE);

    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP;
    x |= SSTATUS_SPIE;
    w_sstatus(x);

    p->trapframe->kernel_trap = (uint64)usertrap;
    p->trapframe->kernel_satp = (uint64)r_satp();
    p->trapframe->kernel_sp  = (uint64)(p->kstack) + PGSIZE;
    p->trapframe->kernel_hartid = r_tp();

    w_sepc(p->trapframe->epc);

    uint64 satp = MAKE_SATP(p->pagetable);

    uint64 trampoline_userret = TRAMPOLINE + ((char*)userret - trampoline);
    ((void (*)(uint64))trampoline_userret)(satp);
}

void kerneltrap()
{
    int which_dev = 0;
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    if((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");
    if(intr_get() != 0)
        panic("kerneltrap: interrupts enabled");

    if((which_dev = devintr()) == 0){
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // give up the CPU if this is a timer interrupt.
    if(which_dev == 2 && myproc() != 0 && myproc()->status == RUNNING) {
        // yield();
    }
    // the yield() may have caused some traps to occur,
    // so restore trap registers for use by kernelvec.S's sepc instruction.
    w_sepc(sepc);
    w_sstatus(sstatus);
}
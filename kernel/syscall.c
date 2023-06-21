#include "syscall.h"
#include "types.h"
#include "proc.h"
#include "utils.h"
#include "vm.h"
#include "string.h"
int fetchaddr(uint64 addr, uint64* ip)
{
    struct proc* p = myproc();
    if (addr >= p->sz || addr + sizeof(uint64) >= p->sz) {
        return -1;
    }
    if (copyin(p->pagetable, (char*)ip, addr, sizeof(*ip)) != 0) {
        return -1;
    }
    return 0;
}
int fetchstr(uint64 addr, char* buf, uint64 max)
{
    struct proc* p = myproc();
    if (copyinstr(p->pagetable, buf, addr, max) < 0) {
        return -1;
    }
    return strlen(buf);
}

static uint64 argraw(int n)
{
    struct proc *p = myproc();
    switch (n) {
    case 0:
    return p->trapframe->a0;
    case 1:
    return p->trapframe->a1;
    case 2:
    return p->trapframe->a2;
    case 3:
    return p->trapframe->a3;
    case 4:
    return p->trapframe->a4;
    case 5:
    return p->trapframe->a5;
    }
    panic("argraw");
    return -1;
}


void argint(int n, int* i)
{
   *i = argraw(n);
}

void argaddr(int n, uint64* ip)
{
    *ip = argraw(n);
}

int argstr(int n, char* buf, int max)
{
    uint64 addr;
    argaddr(n, &addr);
    return fetchstr(addr, buf, max);
}

uint64 sys_exit(void)
{
    int xstatus = -1;
    argint(0, &xstatus);
    exit(xstatus);
    return 0;
}

uint64 (*systemcalls[])() = {
[SYS_test]   sys_exit
};

void syscall()
{
    uint64 a7 = myproc()->trapframe->a7;
    if (a7 < 0 || a7 >= NELEM(systemcalls)) {
        printf("unkown syscall\n");
        myproc()->trapframe->a0 = -1;
        return;
    }
    systemcalls[a7]();
}
#include "riscv.h"
#include "memlayout.h"
#include "param.h"

char stack[4 * 1024];
uint64 timer_scratch[N_CPU][5];
void timervec();
void main();
void start()
{
    // supervisor mode init

    uint64 x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK;
    x |= MSTATUS_MPP_S;
    w_mstatus(x);
    w_mepc((uint64)main);
    w_satp(0);

    w_medeleg(0xffff);
    w_mideleg(0xffff);
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);
    // timer init
    int id = r_mhartid();
    int interval = 10000000;
    *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;
    uint64 *scratch = &timer_scratch[id][0];
    scratch[3] = CLINT_MTIMECMP(id);
    scratch[4] = interval;
    w_mscratch((uint64)scratch);
    w_mtvec((uint64)timervec);
    w_mie(r_mie()| MIE_MTIE);
    w_mstatus(r_mstatus()| MSTATUS_MIE);

    // keep each CPU's hartid in its tp register, for cpuid().
    w_tp(id);
    // switch to supervisor mode and jump to main().
    asm volatile("mret");
}
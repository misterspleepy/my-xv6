#ifndef _UTILS_H
#define _UTILS_H
#include "riscv.h"
// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

void printf(char *fmt, ...);
void printptr(uint64 x);

static void panic(char* msg)
{
    // print debug msg
    intr_off();
    printf(msg);
    while(1) {
    }
}

#endif
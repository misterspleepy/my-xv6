#ifndef _UTILS_H
#define _UTILS_H
// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

void printf(char *fmt, ...);
void printptr(uint64 x);

inline void panic(char* msg)
{
    // print debug msg
    printf(msg);
    while(1) {
    }
}

#endif
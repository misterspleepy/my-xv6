#ifndef _FILE_H_
#define _FILE_H_
#include "types.h"
#include "param.h"
#include "sleeplock.h"
#include "riscv.h"
#include "fs.h"
struct file {
    enum {FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE} type;
    int ref;
    char readable;
    char writable;
    struct pipe* pipe;
    struct inode* ip;
    uint off;
    short major;
};

#define major(dev) ((dev) >> 16 & 0xFFFF)
#define minor(dev) ((dev) & 0xFFFF)
#define mkdev(m, n) ((uint)((m)<<16| (n)))

struct inode {
    uint dev;
    uint inum;
    int ref;
    int valid;

    struct sleeplock lock;
    short type;
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT + 1];
};

struct devsw {
    int (*read)(int, uint64, int);
    int (*write)(int, uint64, int);
};

extern struct devsw devsw[];
#define CONSOLE 1
#endif
#include "file.h"
#include "param.h"
#include "stat.h"
#include "vm.h"
#include "proc.h"
#include "utils.h"
#include "defs.h"
struct {
    struct file files[NFILE];
} ftable;

struct devsw devsw[NDEV];
void stati(struct inode* ip, struct stat* st);
void fileinit()
{}

struct file* filealloc()
{
    for (int i = 0; i < NFILE; i++) {
        if (ftable.files[i].ref != 0) {
            continue;
        }
        ftable.files[i].ref = 1;
        return &ftable.files[i];
    }
    return 0;
}

struct file* filedup(struct file* f)
{
    if (f->ref < 1) {
        panic("filedup\n");
    }
    f->ref++;
    return f;
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file* f, uint64 addr)
{
    struct proc *p = myproc();
    struct stat st;
    
    if(f->type == FD_INODE || f->type == FD_DEVICE){
        ilock(f->ip);
        stati(f->ip, &st);
        iunlock(f->ip);
        if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
        return -1;
        return 0;
    }
    return -1;
}

int fileread(struct file* f, uint64 addr, int n)
{
    int r = 0;

    if (f->readable == 0) {
        return -1;
    }

    if (f->type == FD_PIPE) {
        r = piperead(f->pipe, addr, n);
    } else if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read) {
            return -1;
        }
        r = devsw[f->major].read(1, addr, n);
    } else if (f->type == FD_INODE) {
        if ((r = readi(f->ip, 1, addr, f->off, n)) > 0) {
            f->off += r;
        }
    } else {
        panic("fileread");
    }
    return r;
}

int filewrite(struct file* f, uint64 addr, int n)
{
    int r, ret = 0;
    if (f->writable == 0) {
        return -1;
    }

    if (f->type == FD_PIPE) {
        ret = pipewrite(f->pipe, addr, n);
    } else if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write) {
            return -1;
        }
        ret = devsw[f->major].write(1, addr, n);
    } else if (f->type == FD_INODE) {
        int i = 0;
        int max = BSIZE * 8;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max) {
                n1 = max;
            }
            if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0) {
                f->off += r;
            }
            if (r != n1) {
                break;
            }
            i += r;
        }
        ret = (i == n ? n : -1);
    } else {
        panic("filewrite\n");
    }
    return ret;
}

void fileclose(struct file* f)
{
    struct file ff;
    if (f->ref < 1) {
        panic("filedup\n");
    }
    if (--f->ref > 0) {
        return;
    }
    ff = *f;
    f->ref = 0;
    f->type = FD_NONE;
    if (ff.type == FD_PIPE) {
        pipeclose(ff.pipe, ff.writable);
    } else if (ff.type == FD_INODE || ff.type == FD_DEVICE) {
        iput(ff.ip);
    }
}

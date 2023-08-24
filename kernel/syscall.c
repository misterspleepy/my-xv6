#include "syscall.h"
#include "types.h"
#include "proc.h"
#include "utils.h"
#include "vm.h"
#include "string.h"
#include "buf.h"
#include "fs.h"
#include "stat.h"
#include "fcntl.h"
#include "stat.h"
#include "defs.h"

extern uint ticks;
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

uint64 sys_sbrk(void)
{
    int n = 0;
    int oldsz = myproc()->sz;
    argint(0, &n);
    if (growproc(n)) {
        return -1;
    } 
    return oldsz;
}

uint64 sys_sleep()
{
    int n;
    uint ticks0;
    argint(0, &n);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (killed(myproc())) {
            return -1;
        }
        sleep(&ticks, 0);
    }
    return 0;
}

uint64 sys_fork(void)
{
  return fork();
}

uint64 sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++) {
    if (i >= NELEM(argv)) {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64*)&uarg) < 0) {
      goto bad;
    }
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0) {
      goto bad;
    }
    if (fetchstr(uarg, argv[i], PGSIZE) < 0) {
      goto bad;
    }
  }
  int ret = exec(path, argv);
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++) {
    kfree(argv[i]);
  }
  return ret;
bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++) {
    kfree(argv[i]);
  }
  return -1;
}

/* file related */
// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd(int n, int *pfd, struct file **pf)
{
    int fd;
    struct file *f;

    argint(n, &fd);
    if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
        return -1;
    if(pfd)
        *pfd = fd;
    if(pf)
        *pf = f;
    return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int fdalloc(struct file *f)
{
    int fd;
    struct proc *p = myproc();

    for(fd = 0; fd < NOFILE; fd++){
        if(p->ofile[fd] == 0){
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

uint64 sys_dup(void)
{
    struct file *f;
    int fd;

    if(argfd(0, 0, &f) < 0)
        return -1;
    if((fd=fdalloc(f)) < 0)
        return -1;
    filedup(f);
    return fd;
}

uint64 sys_read(void)
{
    struct file *f;
    int n;
    uint64 p;

    argaddr(1, &p);
    argint(2, &n);
    if(argfd(0, 0, &f) < 0)
        return -1;
    return fileread(f, p, n);
}

uint64 sys_write(void)
{
    struct file *f;
    int n;
    uint64 p;
    
    argaddr(1, &p);
    argint(2, &n);
    if(argfd(0, 0, &f) < 0) {
      return -1;
    }
    return filewrite(f, p, n);
}

uint64 sys_close(void)
{
    int fd;
    struct file *f;

    if(argfd(0, &fd, &f) < 0)
        return -1;
    myproc()->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

uint64 sys_fstat(void)
{
    struct file *f;
    uint64 st; // user pointer to struct stat

    argaddr(1, &st);
    if(argfd(0, 0, &f) < 0)
        return -1;
    return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64 sys_link(void)
{
    char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
    struct inode *dp, *ip;

    if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
        return -1;

    if((ip = namei(old)) == 0){
        return -1;
    }

    ilock(ip);
    if(ip->type == T_DIR){
        iunlockput(ip);
        return -1;
    }

    ip->nlink++;
    iupdate(ip);
    iunlock(ip);

    if((dp = nameiparent(new, name)) == 0)
        goto bad;
    ilock(dp);
    if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
        iunlockput(dp);
        goto bad;
    }
    iunlockput(dp);
    iput(ip);

    return 0;

    bad:
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64 sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  if((dp = nameiparent(path, name)) == 0){
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  return 0;

bad:
  iunlockput(dp);
  return -1;
}

static struct inode* create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    return -1;
  }
  iunlockput(ip);
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    return -1;
  }
  iunlockput(ip);
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  p->cwd = ip;
  return 0;
}

uint64 sys_wait()
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64 sys_pipe()
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

uint64 sys_kill()
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

uint64 sys_getpid()
{
  return myproc()->pid;
}

uint64 sys_uptime()
{
  return -1;
}

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
};

void syscall()
{
    uint64 a7 = myproc()->trapframe->a7;
    if (a7 < 0 || a7 >= NELEM(syscalls)) {
        printf("unkown syscall\n");
        myproc()->trapframe->a0 = -1;
        return;
    }
    myproc()->trapframe->a0 = syscalls[a7]();
}
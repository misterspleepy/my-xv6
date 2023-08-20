struct stat;
/* exec */
int exec(char *path, char **argv);
/* proc */
struct proc;
struct proc* myproc();
int fork();
int killed(struct proc*);
int wait(uint64 addr);
void sleep(void* chan, struct spinlock*);
void wakeup(void* chan);
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);

pagetable_t proc_pagetable(struct proc* proc);
void proc_freepagetable(pagetable_t proc, uint64 sz);
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
/* kmem */
void kinit();
void* kalloc();
void kfree(void* pa);
/* vm */
int copyin(pagetable_t pagetable, char* dst, uint64 srcva, uint64 len);
pte_t* walk(pagetable_t pagetable, uint64 va, int alloc);
uint64 walkaddr(pagetable_t pagetable, uint64 va);
void uvmclear(pagetable_t pagetable, uint64 va);
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm);
/* fs */
struct buf* bread(uint dev, uint blockno);
void brelse(struct buf *b);
void bwrite(struct buf *b);
void fsinit(int dev);
void bzero(uint dev, uint blockno);
int balloc(uint dev);
void bfree(uint dev, uint blockno);
void iinit();
struct inode* iget(uint dev, uint inum);
struct inode* ialloc(uint dev, short type);
void iupdate(struct inode* ip);
struct inode* iget(uint dev, uint inum);
struct inode* idup(struct inode* ip);
void ilock(struct inode* ip);
void iunlock(struct inode* ip);
void itrunc(struct inode* ip);
void iput(struct inode* ip);
void iunlockput(struct inode *ip);
uint bmap(struct inode* ip, uint bn);
void itrunc(struct inode* ip);
void stati(struct inode* ip, struct stat* st);
int readi(struct inode* ip, int user_dst, uint64 dst, uint off, uint n);
int writei(struct inode* ip, int user_src, uint64 src, uint off, uint n);
struct inode* dirlookup(struct inode *dp, char *name, uint *poff);
int dirlink(struct inode *dp, char *name, uint inum);
int namecmp(const char *s, const char *t);
struct inode* namei(char *path);
struct inode* nameiparent(char *path, char *name);
/* file */
struct file;
struct file* filedup(struct file* f);
void fileclose(struct file* f);
int fileread(struct file* f, uint64 addr, int n);
int filestat(struct file* f, uint64 addr);
struct file* filealloc();
void fileinit();
int filewrite(struct file* f, uint64 addr, int n);

/* pipe */
struct pipe;
int pipealloc(struct file **f0, struct file **f1);
void pipeclose(struct pipe* pp, int writable);
int piperead(struct pipe* pp, uint64 va, int n);
int pipewrite(struct pipe* pp, uint64 va, int n);
/* vm */
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);

/* string */
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

/* uart */
void uartputc_sync(int c);
void uartputc(int c);
void uartinit(void);
void consoleintr(int c);
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "utils.h"
#include "buf.h"
#include "string.h"
#include "proc.h"
#include "vm.h"
#define min(a, b) ((a) < (b) ? (a) : (b))
struct superblock sb;
struct buf* bread(uint dev, uint blockno);
void brelse(struct buf *b);
void bwrite(struct buf *b);
static void readsb(int dev, struct superblock* sb)
{
    struct buf* b = bread(dev, 1);
    memmove((void*)sb, (void*)b->data, sizeof(struct superblock));
    brelse(b);
}

void fsinit(int dev)
{
    readsb(dev, &sb);
    if (sb.magic != FSMAGIC) {
        panic("fsinit\n");
    }
}

void bzero(uint dev, uint blockno)
{
    struct buf* b = bread(dev, blockno);
    memset(b->data, 0, sizeof(b->data));
    bwrite(b);
    brelse(b);
}

int balloc(uint dev)
{
    struct buf* bp;
    uint b, bi, m;
    for (b = 0; b < sb.size; b += BPB) {
        bp = bread(dev, BBLOCK(b, sb));
        for (bi = 0; bi < BPB && b + bi < sb.size; bi++) {
            m = 1 << (bi % 8);
            if ((bp->data[bi/8] & m) == 0) {
                bp->data[bi/8] |= m;
                bwrite(bp);
                brelse(bp);
                bzero(dev, b + bi);
                return b + bi;
            }
        }
        brelse(bp);
    }
    printf("balloc: out of blocks\n");
    return 0;
}

void bfree(uint dev, uint blockno)
{
    struct buf* bp;
    uint bi, m;
    bp = bread(dev, BBLOCK(blockno, sb));
    bi = blockno % BPB;
    m = 1 << (bi % 8);
    if ((bp->data[bi / 8] & m) == 0) {
        panic("bfree\n");
    }
    bp->data[bi / 8] &= ~m;
    bwrite(bp);
    brelse(bp);
}

struct {
    struct inode inode[NINODE];
} itable;

void iinit()
{
    for (uint inum = 0; inum < NINODE; inum++) {
       initsleeplock(&itable.inode[inum].lock, "inode");
    }
}

struct inode* iget(uint dev, uint inum);
struct inode* ialloc(uint dev, short type)
{
    struct buf* b;
    struct dinode* di;
    for (uint inum = 1; inum < sb.ninodes; inum++) {
        b = bread(dev, IBLOCK(inum, sb));
        di = (struct dinode*)(b->data) + inum % IPB;
        if (di->type == 0) {
            memset((void*)di, 0, sizeof(struct dinode));
            di->type = type;
            bwrite(b);
            brelse(b);
            return iget(dev, inum);
        }
        brelse(b);
    }
    printf("ialloc: no inodes\n");
    return 0;
}

// copy modified in-memory inode to disk
// Caller must hold ip->lock.
void iupdate(struct inode* ip)
{
    struct buf* b = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode* di;
    di = (struct dinode*)b->data + ip->inum % IPB;
    di->type = ip->type;
    di->major = ip->major;
    di->minor = ip->minor;
    di->nlink = ip->nlink;
    di->size = ip->size;
    memmove((void*)di->addrs, (void*)ip->addrs, sizeof(di->addrs));
    bwrite(b);
    brelse(b);
}

struct inode* iget(uint dev, uint in)
{
    struct inode* ip;
    struct inode* empty = 0;
    for (uint inum = 0; inum < NINODE; inum++) {
        ip = &itable.inode[inum];
        if (ip->ref > 0 && ip->dev == dev && ip->inum == in) {
            ip->ref++;
            return ip;
        }
        if (!empty && ip->ref == 0) {
            empty = ip;
        }
    }
    if (empty == 0 ) {
        panic("iget\n");
    }
    empty->dev = dev;
    empty->inum = in;
    empty->ref = 1;
    empty->valid = 0;
    return empty;
}

struct inode* idup(struct inode* ip)
{
   ip->ref++;
   return ip;
}

void ilock(struct inode* ip)
{
    struct buf* b;
    struct dinode* di;
    if (ip == 0 || ip->ref < 1) {
        panic("ilock\n");
    }
    acquiresleep(&ip->lock);
    if (ip->valid == 0) {
        b = bread(ip->dev, IBLOCK(ip->inum, sb));
        di = (struct dinode*)b->data + ip->inum % IPB;
        ip->type = di->type;
        ip->major = di->major;
        ip->minor = di->minor;
        ip->nlink = di->nlink;
        ip->size = di->size;
        memmove((void*)ip->addrs, (void*)di->addrs, sizeof(ip->addrs));
        brelse(b);
        ip->valid = 1;
        if (ip->type == 0) {
            panic("ilock type\n");
        }
    }
}

void iunlock(struct inode* ip)
{
    if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1) {
        panic("iunlock");
    }
    releasesleep(&ip->lock);
}

void itrunc(struct inode* ip);
void iput(struct inode* ip)
{
    if(ip->ref == 1 && ip->valid && ip->nlink == 0){
        acquiresleep(&ip->lock);
        ip->type = 0;
        itrunc(ip);
        iupdate(ip);
        releasesleep(&ip->lock);
    }
    ip->ref--;
}


// Common idiom: unlock, then put.
void iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

uint bmap(struct inode* ip, uint bn)
{
    struct buf* bp;
    uint addr = 0, *a;
    if (bn < NDIRECT) {
        if ((addr = ip->addrs[bn]) == 0) {
            addr = balloc(ip->dev);
            if (addr == 0) {
                return 0;
            }
            ip->addrs[bn] = addr;
        }
        return addr;
    }
    bn -= NDIRECT;
    if(bn < NINDIRECT){
        if ((addr = ip->addrs[NDIRECT]) == 0) {
            addr = balloc(ip->dev);
            if (addr == 0) {
                return 0;
            }
            ip->addrs[NDIRECT] = addr;
        }
        bp = bread(ip->dev, addr);
        
        a = (uint*)bp->data;
        if ((addr = a[bn]) == 0) {
            addr = balloc(ip->dev);
            if (addr) {
                a[bn] = addr;
                bwrite(bp);
            }
        }
        brelse(bp);
        return addr;
    }
    panic("bmap: out of range");
    return 0;
}

void itrunc(struct inode* ip)
{
    int i, j;
    struct buf *bp;
    uint *a;
    for(i = 0; i < NDIRECT; i++) {
        if(ip->addrs[i]){
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    if(ip->addrs[NDIRECT]) {
        bp = bread(ip->dev, ip->addrs[NDIRECT]);
        a = (uint*)bp->data;
        for(j = 0; j < NINDIRECT; j++){
            if(a[j]) {
                bfree(ip->dev, a[j]);
            }   
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}

void stati(struct inode* ip, struct stat* st)
{
    st->dev = ip->dev;
    st->ino = ip->inum;
    st->type = ip->type;
    st->nlink = ip->nlink;
    st->size = ip->size;
}

int readi(struct inode* ip, int user_dst, uint64 dst, uint off, uint n)
{
    struct buf* bp;
    uint tot, m = 0;
    if (off > ip->size || off + n < off) {
        return 0;
    }
    if (off + n > ip->size) {
        n = ip->size - off;
    }
    for (tot = 0; tot < n; tot += m, off += m, dst += m) {
        uint addr = bmap(ip, off/BSIZE);
        if(addr == 0) {
            break;
        }
        bp = bread(ip->dev, addr);
        m = min(n - tot, BSIZE - off % BSIZE);
        if (either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
            brelse(bp);
            tot = -1;
            break;
        }
        brelse(bp);
    }
    return tot;
}

int writei(struct inode* ip, int user_src, uint64 src, uint off, uint n)
{
    struct buf* bp;
    uint tot, m = 0;
    if (off > ip->size || off + n < off) {
        return -1;
    }
    if (off + n > MAXFILE * BSIZE) {
        return -1;
    }
    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        uint addr = bmap(ip, off / BSIZE);
        if(addr == 0) {
            break;
        }
        bp = bread(ip->dev, addr);
        m = min(n - tot, BSIZE - off % BSIZE);
        if (either_copyin(bp->data + off % BSIZE, user_src, src, m) == -1) {
            brelse(bp);
            tot = -1;
            break;
        }
        bwrite(bp);
        brelse(bp);
    }
    if(off > ip->size) {
        ip->size = off;
    }
    iupdate(ip);
    return tot;
}

// Directories

int namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}


struct inode* dirlookup(struct inode *dp, char *name, uint *poff)
{
    struct dirent de;
    uint off;
    if(dp->type != T_DIR) {
        panic("dirlookup not DIR");
    }
    for (off = 0; off < dp->size; off += sizeof(struct dirent)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(struct dirent)) != sizeof(struct dirent)) {
            panic("dirlookup read");
        }
        if (de.inum == 0) {
            continue;
        }
        if (namecmp(name, de.name) == 0) {
            if (poff) {
                *poff = off;
            }
            return iget(dp->dev, de.inum);
        }
    }
    return 0;
}

int dirlink(struct inode *dp, char *name, uint inum)
{
    uint off;
    struct inode* ip;
    struct dirent de;
    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iput(ip);
        return -1;
    }
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
            panic("dirlink read\n");
        }
        if (de.inum == 0) {
            break;
        }
    }
    safestrcpy(de.name, name, DIRSIZ);
    de.inum = inum;
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        return -1;
    }
    return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
    char *s;
    int len;

    while(*path == '/') {
        path++;
    }
   
    if(*path == 0) {
        return 0;
    }
    
    s = path;
    while(*path != '/' && *path != 0) {
        path++;
    } 
    len = path - s;
    if(len >= DIRSIZ) {
        memmove(name, s, DIRSIZ);
    }
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while(*path == '/'){
        path++;
    }
    return path;
}

static struct inode* namex(char* path, int nameiparent, char* name)
{
    struct inode *ip, *next;
    if (*path == '/') {
        ip = iget(ROOTDEV, ROOTINO);
    } else {
        ip = idup(myproc()->cwd);
    }
    while ((path = skipelem(path, name)) != 0) {
        ilock(ip);
        if (ip->type != T_DIR) {
            iunlockput(ip);
            return 0;
        }
        if (nameiparent && *path == '\0') {
            iunlock(ip);
            return ip;
        }
        if ((next = dirlookup(ip, name, 0)) == 0) {
            iunlockput(ip);
            return 0;
        }
        iunlockput(ip);
        ip = next;
    }
    if (nameiparent) {
        iput(ip);
        return 0;
    }
    return ip;
}

struct inode* namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode* nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
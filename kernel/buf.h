#ifndef _BUF_H_
#define _BUF_H_
#include "sleeplock.h"
#include "param.h"
#include "fs.h"
struct buf {
  struct buf *prev; // LRU cache list
  struct buf *next;
  uint blockno; 
  uint dev;
  uint refcnt;
  struct sleeplock lock;
  int disk;    // does disk "own" buf?
  int valid;   // has data been read from disk?
  uchar data[BSIZE];
};
#endif
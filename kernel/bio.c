// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 质数哈希桶数量
#define NBUCKET 13

struct {
  struct spinlock eviction_lock; // 全局锁：用于串行化缓存未命中时的淘汰过程
  struct spinlock lock[NBUCKET]; // 每个哈希桶独立的一把锁
  struct buf buf[NBUF];
  struct buf head[NBUCKET];      // 哈希桶链表数组
} bcache;

void
binit(void)
{
  initlock(&bcache.eviction_lock, "bcache");

  // 初始化每个哈希桶
  for(int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }
  
  // 【优化 1：均匀发牌】将所有初始缓存块均匀散列到 13 个桶中，杜绝初期争抢
  for(int i = 0; i < NBUF; i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->timestamp = 0;
    int id = i % NBUCKET; // 均匀分配
    b->next = bcache.head[id].next;
    b->prev = &bcache.head[id];
    bcache.head[id].next->prev = b;
    bcache.head[id].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int id = blockno % NBUCKET;

  /*acquire(&bcache.lock[id]);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");*/

  // 1. 高速路径：尝试在对应的哈希桶中查找（仅锁定当前桶）
  acquire(&bcache.lock[id]);
  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 2. 缓存未命中时，绝不立刻动用全局锁！
  // 优先扫描当前自己的桶里有没有空闲块，如果有，直接拿来用
  struct buf *lru_buf = 0;
  uint min_ticks = (uint)-1; 

  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->refcnt == 0 && b->timestamp < min_ticks){
      min_ticks = b->timestamp;
      lru_buf = b;
    }
  }

  if(lru_buf){
    lru_buf->dev = dev;
    lru_buf->blockno = blockno;
    lru_buf->valid = 0;
    lru_buf->refcnt = 1;
    release(&bcache.lock[id]);
    acquiresleep(&lru_buf->lock);
    return lru_buf;
  }

  release(&bcache.lock[id]); // 当前桶真的没资源了，释放它，准备安全地遍历所有桶寻找 LRU

  // 3. 真正需要跨桶驱逐：获取全局驱逐锁
  acquire(&bcache.eviction_lock);
  
  // 二次校验：在等待全局锁的期间，另一个进程可能已经把这块磁盘加载进来了
  acquire(&bcache.lock[id]);
  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[id]);

  lru_buf = 0;
  int lru_bucket = -1;
  min_ticks = -1; 

  // 4. 按照 0 到 NBUCKET-1 的顺序遍历寻找全局 LRU（按顺序加锁可绝对避免死锁）
  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.lock[i]);
    int found_new_min = 0;
    
    for(b = bcache.head[i].next; b != &bcache.head[i]; b = b->next){
      // 引用计数为 0 代表空闲
      if(b->refcnt == 0 && b->timestamp < min_ticks){
        min_ticks = b->timestamp;
        lru_buf = b;
        found_new_min = 1;
      }
    }
    
    // 如果在这个桶中找到了更小的 LRU 块
    if(found_new_min){
      if(lru_bucket != -1){
        release(&bcache.lock[lru_bucket]); // 释放之前持有的、不是最小的那个桶的锁
      }
      lru_bucket = i; // 保留当前包含最小 LRU 块的桶的锁
    } else {
      release(&bcache.lock[i]); // 在这个桶里没找到更小的，直接释放
    }
  }

  if(lru_buf == 0){
    panic("bget: no buffers");
  }

  // 此时，我们仍然且仅持有 lru_bucket 的锁！安全地将其从旧桶中解绑
  lru_buf->prev->next = lru_buf->next;
  lru_buf->next->prev = lru_buf->prev;
  release(&bcache.lock[lru_bucket]);

  // 5. 将提取出来的空闲块插入到我们需要的新桶 (id) 中
  acquire(&bcache.lock[id]);
  lru_buf->next = bcache.head[id].next;
  lru_buf->prev = &bcache.head[id];
  bcache.head[id].next->prev = lru_buf;
  bcache.head[id].next = lru_buf;
  
  lru_buf->dev = dev;
  lru_buf->blockno = blockno;
  lru_buf->valid = 0;
  lru_buf->refcnt = 1;
  release(&bcache.lock[id]);
  
  release(&bcache.eviction_lock);

  acquiresleep(&lru_buf->lock);
  return lru_buf;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int id = b->blockno % NBUCKET;
  acquire(&bcache.lock[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 块被释放时打上时间戳，代替原来的插入链表头操作
    b->timestamp = ticks;
  }
  
  release(&bcache.lock[id]);
}

void
bpin(struct buf *b) {
  int id = b->blockno % NBUCKET;
  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void
bunpin(struct buf *b) {
  int id = b->blockno % NBUCKET;
  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}



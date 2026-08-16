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
  struct spinlock lock[NBUCKET]; // 每个哈希桶独立的一把锁
  struct buf buf[NBUF];
  struct buf head[NBUCKET];      // 哈希桶链表数组
} bcache;

void
binit(void)
{
  // 初始化每个哈希桶
  for(int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }
  
  // 初始化时，将所有初始缓存块均匀分配到 13 个桶中
  for(int i = 0; i < NBUF; i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    int id = i % NBUCKET;
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
retry:
  // 1. 高速路径：尝试在对应的哈希桶中查找
  acquire(&bcache.lock[id]);
  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[id]);

  // 2. 缓存未命中：无状态扫描寻找全局 LRU
  struct buf *victim = 0;
  int victim_bucket = -1;
  uint min_ticks = (uint)-1;

  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.lock[i]);
    for(b = bcache.head[i].next; b != &bcache.head[i]; b = b->next){
      if(b->refcnt == 0 && b->timestamp < min_ticks){
        min_ticks = b->timestamp;
        victim = b;
        victim_bucket = i;
      }
    }
    release(&bcache.lock[i]);
  }

  if(victim == 0){
    panic("bget: no buffers");
  }

  // 3. 严格锁排序 (Lock Ordering)：永远先获取索引较小的锁，杜绝死锁
  if(victim_bucket < id){
    acquire(&bcache.lock[victim_bucket]);
    acquire(&bcache.lock[id]);
  } else if(victim_bucket > id){
    acquire(&bcache.lock[id]);
    acquire(&bcache.lock[victim_bucket]);
  } else {
    // 都在同一个桶里
    acquire(&bcache.lock[id]);
  }

  // 二次安全校验1：在我们刚才没拿锁的时候，有没有别人已经把我们要的 blockno 加载进来了？
  int hit = 0;
  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      hit = 1;
      break;
    }
  }
  
  if(hit){
    // 被别人捷足先登加载了，直接用别人加载好的
    b->refcnt++;
    if(victim_bucket != id) release(&bcache.lock[victim_bucket]);
    release(&bcache.lock[id]);
    acquiresleep(&b->lock);
    return b;
  }

  // 二次安全校验2：我们的 victim 猎物在这期间有没有被别人抢走？
  if(victim->refcnt == 0){
    // 猎物还在！放心地偷走它
    if(victim_bucket != id){
      // 从旧的桶中摘除
      victim->prev->next = victim->next;
      victim->next->prev = victim->prev;
      // 挂载到我们的新桶中
      victim->next = bcache.head[id].next;
      victim->prev = &bcache.head[id];
      bcache.head[id].next->prev = victim;
      bcache.head[id].next = victim;
    }
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    
    if(victim_bucket != id) release(&bcache.lock[victim_bucket]);
    release(&bcache.lock[id]);
    acquiresleep(&victim->lock);
    return victim;
  } else {
    // 猎物被别人抢先一步使用了！没办法，释放锁，从头再来一次
    if(victim_bucket != id) release(&bcache.lock[victim_bucket]);
    release(&bcache.lock[id]);
    goto retry;
  }
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



// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// 将单一结构体改为数组，每个 CPU 一个
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  // 为每个 CPU 初始化一把专属锁
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // 将空闲页放回当前 CPU 的 freelist
  push_off(); // 关中断，保证 cpuid() 返回值的稳定性
  int id = cpuid();
  acquire(&kmem[id].lock);
  
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  
  release(&kmem[id].lock);
  pop_off();  // 恢复中断
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // 从当前 CPU 的 freelist 中分配一个空闲页
  push_off(); // 关中断，保证 cpuid() 返回值的稳定性
  int id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  else {
    // 当前 CPU 没内存了，去窃取别的 CPU 的内存
    for(int i = 0; i < NCPU; i++) {
      if (i == id) continue; // 跳过自己
      
      acquire(&kmem[i].lock);
      if(kmem[i].freelist) {
        r = kmem[i].freelist;
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break; // 偷到一个就够了，退出循环
      }
      release(&kmem[i].lock);
    }
  }
  release(&kmem[id].lock);
  pop_off();  // 恢复中断

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

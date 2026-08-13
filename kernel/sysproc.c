#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "kernel/sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// [新增] trace 系统调用
uint64
sys_trace(void)
{
  int mask;
  // 从寄存器 a0 获取传递的参数 (mask)
  if(argint(0, &mask) < 0)
    return -1;
  // 将 mask 保存到当前进程的结构体中
  myproc()->trace_mask = mask;
  return 0;
}

// [新增] sysinfo 系统调用
uint64
sys_sysinfo(void)
{
  uint64 st; // 用户空间的指针地址
  if(argaddr(0, &st) < 0)
    return -1;

  struct sysinfo info;
  info.freemem = count_free_mem();
  info.nproc = count_process();

  // 使用 copyout 将内核空间的数据安全地拷贝到用户空间指针指向的位置
  if(copyout(myproc()->pagetable, st, (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}
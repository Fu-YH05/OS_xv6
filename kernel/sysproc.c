#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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
  backtrace(); //打印 sleep 时的 backtrace 以通过测试
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

// [新增] sigalarm
uint64
sys_sigalarm(void)
{
  int ticks;
  uint64 handler;
  
  // 获取两个参数：间隔时间，以及函数指针地址
  if(argint(0, &ticks) < 0 || argaddr(1, &handler) < 0)
    return -1;
    
  struct proc *p = myproc();
  p->alarm_interval = ticks;
  p->alarm_handler = (void(*)())handler;
  p->alarm_ticks = 0; // 重置计数
  
  return 0;
}

// [新增] sigreturn
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  
  // 恢复时钟中断打断瞬间的所有寄存器现场
  memmove(p->trapframe, p->alarm_trapframe, sizeof(struct trapframe));
  
  // 退出报警处理状态，允许下次报警重新触发
  p->is_alarming = 0;
  
  // 返回 a0 寄存器里的原始值（因为上面 memmove 覆盖了，系统调用本身返回值也是放 a0，不写这句原程序的 a0 会被破坏）
  return p->trapframe->a0; 
}
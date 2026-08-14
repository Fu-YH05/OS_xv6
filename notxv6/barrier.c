#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  // 1. 获取屏障锁，保证对共享状态（nthread, round）的修改是互斥的
  pthread_mutex_lock(&bstate.barrier_mutex);
  
  // 2. 当前抵达屏障的线程数 + 1
  bstate.nthread++;
  
  // 3. 判断是不是最后一个抵达的线程
  if (bstate.nthread == nthread) {
    // 如果是，开启下一回合
    bstate.round++;
    // 将计数器清零，留给下一回合使用
    bstate.nthread = 0; 
    // [关键] 唤醒所有正在等待该条件变量的线程
    pthread_cond_broadcast(&bstate.barrier_cond); 
  } else {
    // 如果不是最后一个线程，则挂起自身，释放互斥锁，进入等待队列
    // 当被 broadcast 唤醒时，会自动重新获取互斥锁往下执行
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  
  // 4. 所有线程都会执行到这里（无论是最后一个线程，还是被唤醒的等待线程），释放锁
  pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}

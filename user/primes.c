#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void sieve(int pleft[2]) {
    int p;
    close(pleft[1]); // 关闭左侧管道的写端

    // 尝试读取第一个数（必定是当前过滤范围内的素数）
    if (read(pleft[0], &p, sizeof(p)) == 0) {
        close(pleft[0]);
        exit(0);
    }
    printf("prime %d\n", p);

    int pright[2];
    pipe(pright);

    int pid = fork();
    if (pid == 0) {
        // 子进程递归调用下一层筛子
        close(pleft[0]);
        sieve(pright);
    } else {
        close(pright[0]); // 父进程只往右侧写，关闭读端
        int n;
        while (read(pleft[0], &n, sizeof(n)) != 0) {
            if (n % p != 0) { // 过滤不能被 p 整除的数，传给下一个进程
                write(pright[1], &n, sizeof(n));
            }
        }
        close(pleft[0]);
        close(pright[1]); // 数据写完，关闭写端，等待子进程退出
        wait(0);
        exit(0);
    }
}

int main() {
    int p[2];
    pipe(p);

    int pid = fork();
    if (pid == 0) {
        sieve(p);
    } else {
        close(p[0]);
        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, sizeof(i));
        }
        close(p[1]); // 全部写入后关闭写端
        wait(0);
        exit(0);
    }
    return 0;
}

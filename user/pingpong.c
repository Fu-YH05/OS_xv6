#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    int p2c[2]; // 父进程到子进程的管道
    int c2p[2]; // 子进程到父进程的管道
    char buf[1];

    pipe(p2c);
    pipe(c2p);

    int pid = fork();

    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    } else if (pid == 0) { // 子进程
        close(p2c[1]); // 关闭父到子管道的写端
        close(c2p[0]); // 关闭子到父管道的读端

        read(p2c[0], buf, 1);
        printf("%d: received ping\n", getpid());
        
        write(c2p[1], "x", 1);
        
        close(p2c[0]);
        close(c2p[1]);
        exit(0);
    } else { // 父进程
        close(p2c[0]); // 关闭父到子管道的读端
        close(c2p[1]); // 关闭子到父管道的写端

        write(p2c[1], "x", 1);
        
        read(c2p[0], buf, 1);
        printf("%d: received pong\n", getpid());
        
        close(p2c[1]);
        close(c2p[0]);
        exit(0);
    }
}

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs <command> ...\n");
        exit(1);
    }

    char *cmd_argv[MAXARG];
    int cmd_argc = 0;

    // 复制命令行里自带的参数（例如 xargs grep hello，把 grep 和 hello 拷入）
    for (int i = 1; i < argc; i++) {
        cmd_argv[cmd_argc++] = argv[i];
    }

    char buf[512];
    char *p = buf;
    char ch;
    
    // 一次读取一个字符
    while (read(0, &ch, 1) == 1) {
        if (ch == '\n') {
            *p = '\0'; // 字符串结束符
            cmd_argv[cmd_argc] = buf; // 将新读到的一行作为最后一个参数
            cmd_argv[cmd_argc + 1] = 0; // 参数数组以 null 结尾
            
            if (fork() == 0) {
                exec(cmd_argv[0], cmd_argv);
                fprintf(2, "exec %s failed\n", cmd_argv[0]);
                exit(1);
            } else {
                wait(0); // 等待子进程执行完毕
            }
            // 重置指针准备读取下一行
            p = buf;
        } else {
            *p++ = ch;
        }
    }
    
    exit(0);
}

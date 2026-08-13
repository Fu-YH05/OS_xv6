#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    // 检查参数数量，如果没有传入参数则报错提示
    if (argc != 2) {
        fprintf(2, "Usage: sleep <ticks>\n");
        exit(1);
    }

    // 将字符串参数转换为整型
    int ticks = atoi(argv[1]);
    
    // 调用系统调用 sleep
    sleep(ticks);
    
    exit(0);
}
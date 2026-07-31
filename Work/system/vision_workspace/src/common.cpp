/*
 * common.cpp - 共用变量实现
 */

#include "common.h"

//============================================================
//            全局变量定义
//============================================================

volatile int g_running = 1;
TargetQueue g_target_queue;
bool g_use_gui = false;

//============================================================
//            信号处理
//============================================================

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[Main] 收到终止信号，正在停止...\n");
        g_running = 0;
    }
}

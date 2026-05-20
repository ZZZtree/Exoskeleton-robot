/*
 * main_rt.cpp - 增强版实时任务版本
 * 
 *  Created on: 2022-9-20
 *      Author: HanBing
 *      
 *  特性:
 *  - SCHED_FIFO 实时调度
 *  - 内存锁定 (mlockall)
 *  - CPU 亲和性绑定
 *  - 高精度周期任务
 *  - 分离的实时控制线程
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <dlfcn.h>
#include "HYYRobotInterface.h"
#include "user/BscanServer.h"

using namespace HYYRobotBase;

extern int SokcetDemo();
extern void ServoDemo();

//------------------------RTLinux 实时任务设置------------------------
int set_realtime_task() {
    printf("[RT] 正在设置实时任务...\n");
    
    // 预加载关键动态库 (可选，减少运行时加载延迟)
    void* lib_rt = dlopen("librt.so.1", RTLD_NOW);
    void* lib_pthread = dlopen("libpthread.so.0", RTLD_NOW);
    if (lib_rt) dlclose(lib_rt);
    if (lib_pthread) dlclose(lib_pthread);
    
    // 禁用内存换页
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "[RT] 警告: mlockall 失败: %s\n", strerror(errno));
        fprintf(stderr, "[RT] 提示: 使用 'sudo ulimit -l unlimited' 或配置 /etc/security/limits.conf\n");
    } else {
        printf("[RT] 内存锁定成功 (防止换页)\n");
    }
    
    // 获取当前进程
    pid_t pid = getpid();
    printf("[RT] 当前进程 PID: %d\n", pid);
    
    // 设置调度策略为 SCHED_FIFO (实时调度)
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 80;  // 优先级 1-99 (需要 root)
    
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        fprintf(stderr, "[RT] 错误: sched_setscheduler(SCHED_FIFO) 失败: %s\n", strerror(errno));
        fprintf(stderr, "[RT] 需要 root 权限: sudo ./HYYRobotMain_RT --iscopy true\n");
        return -1;
    }
    printf("[RT] 调度策略设置为 SCHED_FIFO, 优先级: %d\n", param.sched_priority);
    
    // 设置 CPU 亲和性 (绑定到 CPU 0)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        fprintf(stderr, "[RT] 警告: sched_setaffinity 失败: %s\n", strerror(errno));
    } else {
        printf("[RT] CPU 亲和性绑定到 CPU 0\n");
    }
    
    printf("[RT] 实时任务设置完成!\n");
    return 0;
}

//------------------------周期任务线程------------------------
#define CONTROL_PERIOD_NS 1000000  // 1ms 控制周期 (1000Hz)

typedef struct {
    int running;
    int period_us;
    int thread_priority;
    const char* thread_name;
} rt_task_config_t;

static volatile int g_rt_running = 1;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[RT] 收到终止信号，正在停止...\n");
        g_rt_running = 0;
    }
}

void* periodic_control_task(void* arg) {
    rt_task_config_t* config = (rt_task_config_t*)arg;
    struct timespec ts;
    static int count = 0;
    static struct timespec start_time;
    
    // 设置本线程的调度策略和优先级
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = config->thread_priority;
    
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        fprintf(stderr, "[RT-Task] 警告: 无法设置线程调度参数: %s\n", strerror(errno));
    }
    
    // 设置线程 CPU 亲和性
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    
    // 锁定线程内存
    mlockall(MCL_CURRENT | MCL_FUTURE);
    
    // 设置线程名称
    pthread_setname_np(pthread_self(), config->thread_name);
    
    // 初始化时间
    clock_gettime(CLOCK_MONOTONIC, &ts);
    start_time = ts;
    
    printf("[RT-Task] 周期控制任务启动 (周期: %d us, 优先级: %d)\n", 
           config->period_us, config->thread_priority);
    
    while (g_rt_running) {
        // 计算下一个周期
        ts.tv_nsec += config->period_us * 1000;
        while (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ts.tv_sec += 1;
        }
        
        // 睡眠到下一个周期
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        
        // 执行控制任务
        count++;
        
        // 每秒打印一次状态
        if (count % 1000 == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - start_time.tv_sec) + 
                            (now.tv_nsec - start_time.tv_nsec) / 1e9;
            printf("[RT-Task] 周期任务执行中: %d 次 (%.1f Hz), 运行时间: %.1f s\n", 
                   count, 1000.0, elapsed);
        }
    }
    
    printf("[RT-Task] 周期任务停止，共执行 %d 次\n", count);
    return NULL;
}

//------------------------实时数据记录线程------------------------
void* data_logging_task(void* arg) {
    struct timespec ts;
    static int log_count = 0;
    
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 70;  // 低于控制线程
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    pthread_setname_np(pthread_self(), "RT_Logger");
    
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    printf("[RT-Log] 数据记录任务启动\n");
    
    while (g_rt_running) {
        ts.tv_nsec += 10000000;  // 10ms 周期
        while (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ts.tv_sec += 1;
        }
        
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        
        log_count++;
        // 数据记录逻辑可以在这里添加
    }
    
    printf("[RT-Log] 数据记录任务停止，共记录 %d 次\n", log_count);
    return NULL;
}

//------------------------主函数------------------------
int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("  HYYRobot Real-Time Control System\n");
    printf("  (RTLinux SCHED_FIFO 实时版本)\n");
    printf("========================================\n");
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    //------------------------设置实时任务------------------------
    int rt_result = set_realtime_task();
    if (rt_result != 0) {
        printf("[Main] 实时任务设置失败 (需要 sudo)，将尝试使用普通调度...\n");
    }
    
    //------------------------initialize----------------------------------
    printf("\n============start robot control======================\n");
    
    int err = 0;
    HYYRobotBase::command_arg arg;
    err = HYYRobotBase::commandLineParser(argc, argv, &arg);
    if (0 != err) {
        fprintf(stderr, "[Main] 命令行解析失败: %d\n", err);
        return -1;
    }
    
    err = HYYRobotBase::system_initialize(&arg);
    if (0 != err) {
        fprintf(stderr, "[Main] 系统初始化失败: %d\n", err);
        return err;
    }
    
    printf("[Main] 系统初始化成功\n");
    
    //-----------------------创建实时线程------------------------
    pthread_t control_thread, log_thread;
    rt_task_config_t control_config = {
        .running = 1,
        .period_us = 1000,      // 1ms = 1000us
        .thread_priority = 90,  // 高优先级
        .thread_name = "RT_Control"
    };
    
    rt_task_config_t log_config = {
        .running = 1,
        .period_us = 10000,      // 10ms
        .thread_priority = 70,   // 较低优先级
        .thread_name = "RT_Logger"
    };
    
    // 创建周期控制线程
    if (pthread_create(&control_thread, NULL, periodic_control_task, &control_config) != 0) {
        fprintf(stderr, "[Main] 警告: 控制线程创建失败: %s\n", strerror(errno));
    } else {
        printf("[Main] 周期控制线程创建成功\n");
    }
    
    // 创建数据记录线程
    if (pthread_create(&log_thread, NULL, data_logging_task, &log_config) != 0) {
        fprintf(stderr, "[Main] 警告: 日志线程创建失败: %s\n", strerror(errno));
    } else {
        printf("[Main] 数据记录线程创建成功\n");
    }
    
    //------------------------执行伺服演示------------------------
    printf("[Main] 启动伺服演示...\n");
    ServoDemo();
    
    //------------------------等待------------------------
    printf("[Main] 进入暂停状态 (按 Ctrl+C 退出)...\n");
    pause();
    
    // 清理
    g_rt_running = 0;
    
    if (control_thread) {
        pthread_join(control_thread, NULL);
    }
    if (log_thread) {
        pthread_join(log_thread, NULL);
    }
    
    printf("[Main] 程序结束\n");
    return 0;
}

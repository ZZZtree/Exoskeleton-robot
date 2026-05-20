/*
 * rt_control.h - RTLinux实时任务设置和电机控制模块
 *
 * 功能：
 * - RTLinux实时任务配置（SCHED_FIFO调度、内存锁定、CPU亲和性）
 * - 视觉伺服电机控制
 */

#ifndef RT_CONTROL_H
#define RT_CONTROL_H

#include "common.h"

//============================================================
//            RTLinux 实时任务设置
//============================================================

/**
 * 设置主线程为实时任务
 * - 锁定内存防止换页
 * - 设置 SCHED_FIFO 调度策略
 * - 绑定到 CPU 0
 * @return 0成功，-1失败
 */
int set_main_thread_realtime();

//============================================================
//            线程亲和性设置
//============================================================

/**
 * 设置线程绑定到指定CPU核心
 */
void set_thread_affinity(pthread_t thread, int cpu_id, const char* thread_name);

/**
 * 设置线程绑定到多个CPU核心
 */
void set_thread_affinity_multi(pthread_t thread, int* cpu_ids, int num_cpus, const char* thread_name);

//============================================================
//            视觉伺服电机控制
//============================================================

/**
 * 实时电机控制主循环（视觉伺服）
 * - 基于RTimer总线周期定时
 * - 结合正弦基准运动和视觉目标跟踪
 */
void rt_motor_control_with_vision();

#endif // RT_CONTROL_H

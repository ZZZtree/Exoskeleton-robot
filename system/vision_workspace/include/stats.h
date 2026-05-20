/*
 * stats.h - CPU和性能统计模块
 *
 * 功能：
 * - CPU使用率统计（基于 /proc/self/stat）
 * - 帧率、FPS、检测延迟等性能指标统计
 * - CPU监控线程管理
 */

#ifndef STATS_H
#define STATS_H

#include <atomic>
#include <mutex>
#include <deque>
#include <cstring>
#include <unistd.h>
#include <thread>
#include <chrono>

//============================================================
//            CPU统计
//============================================================

struct CPUStats {
    std::deque<float> cpu_usage_history;
    std::mutex cpu_mutex;
    float avg_cpu = 0;
    float max_cpu = 0;
    float min_cpu = 100;
    std::atomic<int> thread_count{0};
    std::atomic<bool> should_stop{false};

    // 程序实际 CPU 时间（使用 clock_gettime）
    struct timespec start_time = {0, 0};
    bool initialized = false;

    // 获取当前进程的总 CPU 时间（user + system）
    static long long get_process_cpu_time();

    void init();
    float getProcessCPUUsage();
    void update(float cpu_percent);
    void print_cpu_summary(const char* mode_name);
};

//============================================================
//            性能统计
//============================================================

struct PerformanceStats {
    std::atomic<int> total_frames{0};
    std::atomic<int> detect_count{0};
    std::atomic<int> track_count{0};
    std::atomic<int> dropped_frames{0};
    std::atomic<int> rgb_frames{0};
    std::atomic<int> ir_frames{0};

    std::mutex latency_mutex;
    float total_detect_latency = 0;

    std::deque<float> fps_history;
    std::mutex history_mutex;

    float avg_fps = 0;
    float avg_detect_latency = 0;
    float max_fps = 0;
    float min_fps = 9999;

    CPUStats cpu_stats;

    void record_frame(bool is_detect = false, float latency_ms = 0);
    void record_rgb_frame();
    void record_ir_frame();
    void record_dropped();
    void update_fps(float fps);
    void print_summary(const char* mode_name);
};

//============================================================
//            CPU监控线程
//============================================================

void cpu_monitor_thread_func(std::atomic<bool>& running, PerformanceStats& stats, int interval_ms);

#endif // STATS_H

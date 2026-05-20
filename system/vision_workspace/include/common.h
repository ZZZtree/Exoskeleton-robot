/*
 * common.h - 共用头文件和全局变量
 *
 * 架构说明：
 * - 定义所有模块共享的类型、常量和全局变量
 * - 被所有其他模块包含
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <chrono>
#include <deque>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <thread>
#include <chrono>

//============================================================
//            宏定义
//============================================================

#define YOLO_CONF_THRESH  0.40f
#define YOLO_NMS_THRESH  0.30f
#define FRAME_BUFFER_SIZE 2
#define DETECT_INTERVAL   6
#define CPU_MONITOR_INTERVAL_MS 1000

#define EPOLL_MAX_EVENTS 10
#define EPOLL_TIMEOUT_MS 10
#define FRAME_QUEUE_SIZE 2

//============================================================
//            机器人相关
//============================================================

#include "HYYRobotInterface.h"
#include "user/BscanServer.h"
using namespace HYYRobotBase;

//============================================================
//            检测结果结构
//============================================================

struct DetectedTarget {
    float cx, cy;     // 归一化中心坐标 [0,1]
    float width, height;
    int label;
    float prob;
    bool valid;
    DetectedTarget() : cx(0), cy(0), width(0), height(0), label(-1), prob(0), valid(false) {}
};

//============================================================
//            目标队列（视觉线程写入，电机线程读取）
//============================================================

class TargetQueue {
    std::deque<DetectedTarget> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t max_size_;
public:
    TargetQueue(size_t max_size = 4) : max_size_(max_size) {}
    void push(const DetectedTarget& t) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= max_size_) queue_.pop_front();
        queue_.push_back(t);
    }
    bool try_pop(DetectedTarget& t) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        t = queue_.back();
        return true;
    }
    bool empty() {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
};

//============================================================
//            全局变量
//============================================================

extern volatile int g_running;
extern TargetQueue g_target_queue;
extern bool g_use_gui;

//============================================================
//            信号处理
//============================================================

void signal_handler(int sig);

#endif // COMMON_H

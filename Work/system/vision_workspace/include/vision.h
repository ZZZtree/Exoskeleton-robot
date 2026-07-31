/*
 * vision.h - 视觉线程主函数
 *
 * 功能：
 * - Epoll双流模式视觉处理（RGB检测 + IR显示）
 * - RGB检测线程 + ByteTrack追踪
 * - 与电机控制线程通过TargetQueue通信
 */

#ifndef VISION_H
#define VISION_H

#include "common.h"
#include "stats.h"
#include "detector.h"
#include "camera.h"
#include "rt_control.h"

/**
 * 视觉线程主函数（非实时 SCHED_OTHER）
 * @return nullptr
 */
void* vision_thread_func(void*);

#endif // VISION_H

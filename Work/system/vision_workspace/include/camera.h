/*
 * camera.h - V4L2相机驱动和Epoll管理器模块
 *
 * 功能：
 * - V4L2相机设备初始化和帧获取
 * - 支持多种像素格式 (YUYV, UYVY, MJPEG, RGB)
 * - Epoll事件驱动双摄像头管理 (RGB检测 + IR显示)
 */

#ifndef CAMERA_H
#define CAMERA_H

#include "common.h"
#include <opencv2/opencv.hpp>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unordered_map>
#include <string>
#include <vector>

//============================================================
//            相机配置结构
//============================================================

struct CameraConfig {
    int fd = -1;
    void* bufs[4] = {nullptr};
    size_t buf_len[4] = {0};
    int width = 0, height = 0;
    __u32 pixel_format = 0;
    std::string device_path;
    std::string device_name;
    bool is_yuyv = false;
    bool is_uyvy = false;
    bool is_rgb = false;
    bool is_bgr = false;
    bool is_grey = false;
    bool is_mjpeg = false;
};

//============================================================
//            相机扫描函数
//============================================================

/**
 * 扫描并分类视频设备为 RGB 和 IR
 * @param rgb_devices 输出RGB设备列表
 * @param ir_devices 输出IR设备列表
 */
void scan_cameras(std::vector<std::string>& rgb_devices, std::vector<std::string>& ir_devices);

/**
 * 查找RGB摄像头设备
 * @param out_device_path 输出设备路径
 * @return true成功
 */
bool find_rgb_camera(std::string& out_device_path);

//============================================================
//            相机基本操作
//============================================================

/**
 * 初始化相机
 * @param dev 设备路径
 * @param cam 输出相机配置
 * @return true成功
 */
bool init_camera(const char* dev, CameraConfig& cam);

/**
 * 清理相机资源
 */
void cleanup_camera(CameraConfig& cam);

/**
 * 获取单帧
 * @param cam 相机配置
 * @param bgr 输出BGR图像
 * @return true成功
 */
bool grab_frame(CameraConfig& cam, cv::Mat& bgr);

//============================================================
//            检测是否有可用的图形界面
//============================================================

/**
 * 检查DISPLAY环境变量和运行环境
 * @return true表示有GUI可用
 */
bool check_display_available();

//============================================================
//            V4L2 Epoll事件驱动摄像头管理器
//============================================================

class V4L2EpollManager {
public:
    struct CameraContext {
        CameraConfig* cam;
        std::deque<cv::Mat> frame_queue;
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        bool is_rgb;
    };

    V4L2EpollManager();
    ~V4L2EpollManager();

    /**
     * 初始化Epoll
     * @return true成功
     */
    bool init();

    /**
     * 清理资源
     */
    void cleanup();

    /**
     * 添加摄像头到Epoll
     * @param cam 相机配置
     * @param is_rgb 是否为RGB摄像头
     * @return true成功
     */
    bool add_camera(CameraConfig& cam, bool is_rgb);

    /**
     * 启动事件循环
     * @return true成功
     */
    bool start();

    /**
     * 停止事件循环
     */
    void stop();

    /**
     * 获取帧（阻塞等待）
     * @param cam 相机配置
     * @param frame 输出帧
     * @param timeout_ms 超时毫秒
     * @return true成功获取
     */
    bool get_frame(CameraConfig& cam, cv::Mat& frame, int timeout_ms = 0);

    /**
     * 非阻塞获取帧
     */
    bool try_get_frame(CameraConfig& cam, cv::Mat& frame);

    /**
     * 获取队列大小
     */
    size_t get_queue_size(CameraConfig& cam);

    /**
     * 清空队列
     */
    void clear_queue(CameraConfig& cam);

private:
    void event_loop();
    void process_camera_data(CameraContext* ctx);

    int epoll_fd = -1;
    std::unordered_map<int, CameraContext*> contexts;
    std::atomic<bool> running{false};
    std::thread event_thread;
    int wakeup_fd = -1;
};

#endif // CAMERA_H

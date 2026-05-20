/*
 * vision.cpp - 视觉线程主函数实现
 *
 * 架构：
 * - 主线程 (CPU 1): 事件驱动循环，获取IR帧用于显示和跟踪
 * - RGB检测线程 (CPU 2): 每隔DETECT_INTERVAL帧进行一次YOLO检测
 * - CPU监控线程 (CPU 3): 统计CPU使用率
 */

#include "vision.h"

//============================================================
//            视觉线程主函数
//============================================================

void* vision_thread_func(void*) {
    printf("\n========== Epoll 双流模式 ==========\n");
    printf("架构: RGB检测 + IR显示 + Epoll事件驱动\n\n");

    pthread_setname_np(pthread_self(), "Vision_NRT");
    set_thread_affinity(pthread_self(), 1, "Vision_NRT");

    // 扫描摄像头
    std::vector<std::string> rgb_devices, ir_devices;
    scan_cameras(rgb_devices, ir_devices);

    if (rgb_devices.empty()) {
        fprintf(stderr, "[Vision-NRT] 错误: 未找到 RGB 摄像头\n");
        return nullptr;
    }

    // 初始化 RGB 摄像头 (用于检测)
    CameraConfig cam_rgb;
    if (!init_camera(rgb_devices.back().c_str(), cam_rgb)) {
        fprintf(stderr, "[Vision-NRT] RGB 摄像头初始化失败\n");
        return nullptr;
    }

    // 初始化 IR 摄像头 (用于显示)
    CameraConfig cam_ir;
    bool has_ir = false;
    if (!ir_devices.empty()) {
        if (init_camera(ir_devices.back().c_str(), cam_ir)) {
            has_ir = true;
            printf("[Vision-NRT] IR 摄像头已连接\n");
        } else {
            printf("[Vision-NRT] IR 摄像头初始化失败\n");
        }
    } else {
        printf("[Vision-NRT] 未找到 IR 摄像头\n");
    }

    // 加载 YOLOv5 模型
    YoloDetector detector;
    int num_cpus = std::thread::hardware_concurrency();
    if (detector.load("yolov5s.ncnn.param", "yolov5s.ncnn.bin", num_cpus > 2 ? num_cpus : 2) != 0) {
        fprintf(stderr, "[Vision-NRT] YOLOv5 模型加载失败\n");
        cleanup_camera(cam_rgb);
        if (has_ir) cleanup_camera(cam_ir);
        return nullptr;
    }

    // 初始化 Epoll 管理双流
    V4L2EpollManager epoll_mgr;
    if (!epoll_mgr.init()) {
        fprintf(stderr, "[Vision-NRT] Epoll 初始化失败\n");
        cleanup_camera(cam_rgb);
        if (has_ir) cleanup_camera(cam_ir);
        return nullptr;
    }

    // 添���摄像头到 epoll
    epoll_mgr.add_camera(cam_rgb, true);      // RGB 用于检测
    if (has_ir) {
        epoll_mgr.add_camera(cam_ir, false);   // IR 用于显示/跟踪
    }
    epoll_mgr.start();

    // 初始化追踪器和统计
    ByteTrack tracker;
    PerformanceStats stats;
    stats.cpu_stats.thread_count.store(has_ir ? 5 : 4);

    std::atomic<bool> running(true);//初始值为true
    std::atomic<int> rgb_frame_count{0};//初始值为0
    std::atomic<int> ir_frame_count{0};
    std::atomic<int> detect_count{0};

    std::vector<obj_confect> latest_detections;//latest_detections列表里存储obj_confect结构体信息
    std::mutex detection_mutex;//创建了一把锁

    // CPU 监控线程
    std::thread cpu_thread(cpu_monitor_thread_func, std::ref(running), std::ref(stats), CPU_MONITOR_INTERVAL_MS);//创建并启动线程(线程要跑的函数，停止开关，统计数据，监控间隔)
    pthread_setname_np(cpu_thread.native_handle(), "Vision_CPUmon");//cpu_thread.native_handle()，获取线程ID， "Vision_CPUmon"给线程起名字
    set_thread_affinity(cpu_thread.native_handle(), 3, "Vision_CPUmon");//线程绑定到核心上

    // ========== RGB 检测线程 ==========
    std::thread detect_thread([&]() {
        pthread_setname_np(pthread_self(), "Vision_Detect");
        set_thread_affinity(pthread_self(), 2, "Vision_Detect");
        printf("[Vision-Detect] RGB 检测线程启动\n");

        cv::Mat frame;
        int local_count = 0;

        while (running) {
            if (!epoll_mgr.get_frame(cam_rgb, frame, 10)) {
                continue;//如果本次没拿到帧，则直接回到while,重新来一遍
            }

            local_count++;//每拿到一帧RGB画面，本地计数+1
            rgb_frame_count.store(local_count);//把当前帧号，同步给主线程
            stats.record_rgb_frame();//记录帧率统计

            // 每隔 DETECT_INTERVAL 帧进行一次 YOLO 检测
            if (local_count % DETECT_INTERVAL == 0) {
                auto t0 = std::chrono::steady_clock::now();//std::chrono::steady_clock::now(); 获取当前时间戳，计算代码运行了多久
                std::vector<obj_confect> local_dets;//存储这一帧检测到的所有目标
                detector.detect(frame, local_dets);//调用yolo，把画面里的物体找出来，放入local_dets
                auto t1 = std::chrono::steady_clock::now();

                float latency = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();//计算检测耗时；duration_cast<std::chrono::milliseconds>转成毫秒

                {
                    std::lock_guard<std::mutex> lock(detection_mutex);
                    latest_detections = local_dets;//把检测结果放进全局列表
                }
                detect_count.fetch_add(1);//检测次数安全+1
                stats.record_frame(true, latency);//记录每一帧的数据，true表示是一次真的检测
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        printf("[Vision-Detect] 检测线程退出\n");
    });

    if (g_use_gui) {
        std::string win_name = has_ir ? "EPOLL DUAL STREAM (RGB+IR)" : "EPOLL SINGLE STREAM";//窗口名字
        cv::namedWindow(win_name, cv::WINDOW_NORMAL);//WINDOW_NORMAL窗口可以缩放
    }

    printf("[Vision-NRT] 开始主循环 (%s)...\n", has_ir ? "RGB检测 + IR跟踪" : "单RGB流");

    // 显示帧率统计
    int display_count = 0;
    auto fps_time = std::chrono::steady_clock::now();
    auto print_time = std::chrono::steady_clock::now();

    while (running) {
        // 从 IR 摄像头获取帧用于跟踪和显示（如果没有 IR 则用 RGB）
        cv::Mat display_frame;
        CameraConfig& track_cam = has_ir ? cam_ir : cam_rgb;
        if (!epoll_mgr.get_frame(track_cam, display_frame, 3)) {//从track_cam拿一一帧摄像头放在display_frame,等待时间为3ms
            std::this_thread::sleep_for(std::chrono::milliseconds(1));//从摄像头拿一帧画面，拿不到就等 1 毫秒再重试，
            continue;
        }

        ir_frame_count++;
        stats.record_ir_frame();
        display_count++;

        // 计算显示帧率
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - fps_time).count();
        if (elapsed_us >= 500000) {//每过0.5秒，算一次帧率
            float render_fps = display_count * 1000000.0f / elapsed_us;
            stats.update_fps(render_fps);//把fps存进统计
            display_count = 0;//清零重新计数
            fps_time = now;
        }

        // 获取最新检测结果
        std::vector<obj_confect> current_dets;
        {
            std::lock_guard<std::mutex> lock(detection_mutex);
            current_dets = latest_detections;
        }

        auto tracks = tracker.update(current_dets);//对比前后帧，分配ID
        stats.record_frame();//记录一帧，统计帧率

        // 显示界面
        if (g_use_gui) {
            std::string win_name = has_ir ? "EPOLL DUAL (RGB检测+IR跟踪)" : "EPOLL SINGLE";
            cv::Mat display = display_frame.clone();

            // 绘制跟踪框
            for (const auto& t : tracks) {
                cv::Rect r(t.cx - t.w*0.5f, t.cy - t.h*0.5f, t.w, t.h);
                cv::rectangle(display, r, cv::Scalar(255, 0, 255), 2);
                char txt[32];
                snprintf(txt, sizeof(txt), "ID:%d", t.track_id);
                cv::putText(display, txt, cv::Point(r.x, r.y - 5),
                           cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 255), 2);
            }

            // 信息面板
            cv::rectangle(display, cv::Rect(0, 0, 600, 130), cv::Scalar(0,0,0), -1);
            char info[128];

            bool is_detect_frame = (rgb_frame_count.load() % DETECT_INTERVAL == 0);
            snprintf(info, sizeof(info), "跟踪帧率:%.1f FPS | RGB检测:%d | 目标:%zu | CPU:%.1f%%",
                    stats.avg_fps, detect_count.load(), tracks.size(), stats.cpu_stats.avg_cpu);
            cv::putText(display, info, cv::Point(10, 25),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

            snprintf(info, sizeof(info), "RGB帧:%d | IR跟踪:%d | %s",
                    rgb_frame_count.load(), ir_frame_count.load(),
                    has_ir ? "[RGB检测+IR跟踪]" : "[单RGB流]");
            cv::putText(display, info, cv::Point(10, 50),
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(200, 200, 200), 1);

            snprintf(info, sizeof(info), "检测延迟:%.1fms | %s",
                    stats.avg_detect_latency, is_detect_frame ? "[检测帧]" : "[预测帧]");
            cv::putText(display, info, cv::Point(10, 75),
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(200, 200, 200), 1);

            snprintf(info, sizeof(info), "RGB Queue:%d | IR Queue:%d",
                    (int)epoll_mgr.get_queue_size(cam_rgb),
                    has_ir ? (int)epoll_mgr.get_queue_size(cam_ir) : (int)epoll_mgr.get_queue_size(cam_rgb));
            cv::putText(display, info, cv::Point(10, 100),
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(200, 200, 200), 1);

            snprintf(info, sizeof(info), "Epoll双流: RGB检测 + IR跟踪");
            cv::putText(display, info, cv::Point(10, 125),
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(100, 100, 100), 1);

            cv::imshow(win_name, display);
            int key = cv::waitKey(1);
            if (key == 27) { g_running = 0; running = false; }
        } else {
            // 无 GUI 模式
            auto print_now = std::chrono::steady_clock::now();
            auto print_elapsed = std::chrono::duration_cast<std::chrono::seconds>(print_now - print_time).count();
            if (print_elapsed >= 1) {
                printf("[Vision-NRT] 显示:%.1f FPS | RGB检测:%d | IR:%d | 目标:%zu | CPU:%.1f%%\n",
                       stats.avg_fps, detect_count.load(), ir_frame_count.load(),
                       tracks.size(), stats.cpu_stats.avg_cpu);
                print_time = print_now;
            }
        }

        // 发送目标给电机控制
        DetectedTarget target;
        if (!current_dets.empty()) {
            int best_idx = 0;
            for (int i = 1; i < (int)current_dets.size(); i++) {
                if (current_dets[i].prob > current_dets[best_idx].prob) best_idx = i;
            }
            const auto& d = current_dets[best_idx];
            target.cx = (d.rect.x + d.rect.width * 0.5f) / display_frame.cols;
            target.cy = (d.rect.y + d.rect.height * 0.5f) / display_frame.rows;
            target.width = d.rect.width / display_frame.cols;
            target.height = d.rect.height / display_frame.rows;
            target.label = d.label;
            target.prob = d.prob;
            target.valid = true;
            g_target_queue.push(target);
        }
    }

    // 清理
    running = false;
    detect_thread.join();
    cpu_thread.join();
    epoll_mgr.stop();
    epoll_mgr.cleanup();
    cleanup_camera(cam_rgb);
    if (has_ir) cleanup_camera(cam_ir);

    if (g_use_gui) {
        cv::destroyWindow("EPOLL DUAL STREAM");
    }

    stats.print_summary("Epoll双流模式");
    printf("[Vision-NRT] 视觉线程退出\n");
    return nullptr;
}

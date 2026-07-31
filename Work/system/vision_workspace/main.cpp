/*
 * main.cpp - 主入口文件
 *
 * 架构说明：
 * - 主线程: SCHED_FIFO (优先级 90) 实时电机控制，使用 RTimer 总线周期定时
 * - 视觉线程: SCHED_OTHER (优先级 0) 非实时 YOLOv5 目标检测，独立运行
 * - 两线程通过线程安全队列传递目标位置，实现视觉伺服
 *
 * 编译: bash compile_robot_vision.sh
 * 或使用 CMake: mkdir build && cd build && cmake .. && make
 */

#include "common.h"
#include "rt_control.h"
#include "vision.h"
#include "camera.h"

extern int SokcetDemo();

//============================================================
//                    主函数
//============================================================

int main(int argc, char* argv[])
{
    //------------------------预防性 GUI 禁用------------------------
    // 在任何 OpenCV 调用之前设置，禁用所有 OpenCV HighGUI 后端
    // 防止 GTK/Qt 后端在无头环境中崩溃
    setenv("OPENCV_VIDEOIO_PRIORITY_MSMF", "0", 0);
    setenv("OPENCV_VIDEOIO_PRIORITY", "0", 0);
    // 禁用 OpenCV 的窗口自动创建
    setenv("OPENCV_WIN_DISABLE_CREATE", "1", 0);

    printf("========================================\n");
    printf("  HYYRobot + YOLOv5 视觉伺服系统\n");
    printf("  主线程: RTLinux SCHED_FIFO (电机)\n");
    printf("  视觉线程: SCHED_OTHER (目标检测)\n");
    printf("========================================\n");

    // 设置信号处理
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    //------------------------RTLinux 实时任务设置------------------------
    if (set_main_thread_realtime() != 0) {
        printf("[Main] 警告: 实时任务设置失败，电机控制可能无法保证确定性\n");
    }

    //------------------------命令行参数解析------------------------
    // 注意：必须先解析 --gui/--nogui，然后从 argv 中移除，再传给 robot 命令解析器
    // 否则 commandLineParser 会把 --gui 当作未知参数而失败
    std::vector<char*> filtered_argv;
    filtered_argv.push_back(argv[0]);  // 程序名

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--nogui") == 0) {
            g_use_gui = false;
            printf("[Main] 命令行指定: 禁用 GUI 显示 (--nogui)\n");
        } else if (strcmp(argv[i], "--gui") == 0) {
            g_use_gui = true;
            printf("[Main] 命令行指定: 启用 GUI 显示 (--gui)\n");
        } else {
            filtered_argv.push_back(argv[i]);  // 保留其他参数
        }
    }

    // 构建过滤后的参数列表（供 robot 命令解析器使用）
    int new_argc = (int)filtered_argv.size();
    char** new_argv = filtered_argv.data();

    //------------------------机器人系统初始化------------------------
    printf("\n============start robot control======================\n");

    int err = 0;
    HYYRobotBase::command_arg arg;
    err = HYYRobotBase::commandLineParser(new_argc, new_argv, &arg);
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

    // 检测是否有图形界面 (仅当命令行未指定时)
    static bool gui_checked = false;
    if (!gui_checked && g_use_gui) {
        if (check_display_available()) {
            g_use_gui = true;
            printf("[Main] 检测到可用 DISPLAY 环境变量，启用 GUI 显示\n");
        } else {
            g_use_gui = false;
            printf("[Main] 无可用图形界面，禁用 GUI 显示\n");
        }
        gui_checked = true;
    } else if (!g_use_gui) {
        printf("[Main] 使用 --nogui 参数，GUI 已禁用\n");
    }

    //------------------------启动视觉线程（非实时）------------------------
    pthread_t vision_tid;
    printf("[Main] 启动视觉检测线程（非实时 SCHED_OTHER）...\n");
    if (pthread_create(&vision_tid, NULL, vision_thread_func, NULL) != 0) {
        fprintf(stderr, "[Main] 视觉线程创建失败: %s\n", strerror(errno));
    } else {
        printf("[Main] 视觉检测线程已启动\n");
    }

    //------------------------实时电机控制主循环------------------------
    printf("[Main] 启动实时电机控制（SCHED_FIFO 主线程）...\n");
    rt_motor_control_with_vision();

    //------------------------清理------------------------
    g_running = 0;

    if (vision_tid) {
        pthread_join(vision_tid, NULL);
    }

    printf("[Main] 程序结束\n");
    return 0;
}

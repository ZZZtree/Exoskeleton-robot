/*
 * rt_control.cpp - RTLinux实时任务设置和电机控制模块实现
 */

#include "rt_control.h"

//============================================================
//            RTLinux 实时任务设置实现
//============================================================

int set_main_thread_realtime() {
    printf("[RT-Main] 设置主线程为实时任务...\n");

    // 1. 锁定内存，防止换页
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {//给Linux系统下的主线程开启实时运行模式，锁定内存，不交换到硬盘。MCL_CURRENT：锁定现在已经用了的内存MCL_FUTURE：锁定未来将要申请的内存
        fprintf(stderr, "[RT-Main] 警告: mlockall 失败: %s\n", strerror(errno));
        fprintf(stderr, "[RT-Main] 提示: 使用 'sudo ulimit -l unlimited'\n");
    } else {
        printf("[RT-Main] 内存锁定成功\n");
    }
    // 2. 设置 SCHED_FIFO 调度策略
    struct sched_param param;
    memset(&param, 0, sizeof(param));//把param里的所有变量清零
    param.sched_priority = 90;  // 高优先级 (1-99)

    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) { //设置SCHED_FIFO模式，0为当前线程
        fprintf(stderr, "[RT-Main] 错误: sched_setscheduler 失败: %s\n", strerror(errno));
        fprintf(stderr, "[RT-Main] 需要 root 权限: sudo ./robot_vision --iscopy true\n");
        return -1;
    }
    printf("[RT-Main] 调度策略: SCHED_FIFO, 优先级: %d\n", param.sched_priority);

    // 3. CPU 亲和性绑定到 CPU 0
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);//清空聊表，一开始不让县城在任何CPU上跑  //cpu_zero是一个宏
    CPU_SET(0, &cpuset);//将0号cpu加入列表，只运行线程在0号cpu上跑
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {//通过sched_setaffinity绑定，0代表这个当前进程，sizeof(cpu_set_t)代表读取整个cpuset
        fprintf(stderr, "[RT-Main] 警告: sched_setaffinity 失败: %s\n", strerror(errno));
    } else {
        printf("[RT-Main] CPU 亲和性绑定到 CPU 0\n");
    }

    printf("[RT-Main] 主线程实时任务设置完成!\n");
    return 0;
}

//============================================================
//            线程亲和性设置实现
//============================================================

void set_thread_affinity(pthread_t thread, int cpu_id, const char* thread_name) {//要绑定的线程，绑定到哪个核心，线程名字
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0) {//pthread_setaffinity_np：专门给posix线程设置亲和性的函数，是线程库函数。sched_setaffinity：linux调用，只能给自己设定
        fprintf(stderr, "[%s] 警告: 线程亲和性设置失败: %s\n", thread_name, strerror(errno));
    } else {
        printf("[%s] CPU 亲和性绑定到 CPU %d\n", thread_name, cpu_id);
    }
}

void set_thread_affinity_multi(pthread_t thread, int* cpu_ids, int num_cpus, const char* thread_name) {//把一个线程同时绑到多个cpu核心上运行
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < num_cpus; i++) {
        CPU_SET(cpu_ids[i], &cpuset);//所有cpu都加入允许列表
    }
    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0) {//绑定
        fprintf(stderr, "[%s] 警告: 线程亲和性设置失败: %s\n", thread_name, strerror(errno));
    } else {
        printf("[%s] CPU 亲和性绑定到 CPUs: ", thread_name);
        for (int i = 0; i < num_cpus; i++) {
            printf("%d ", cpu_ids[i]);
        }
        printf("\n");
    }
}

//============================================================
//            视觉伺服电机控制实现
//============================================================

void rt_motor_control_with_vision() {
    RTimer timer;
    initUserTimer(&timer, 0, 1);

    const char* robot_name = get_name_robot_device(get_deviceName(0, NULL), 0);
    double td = get_control_cycle(get_deviceName(0, NULL));
    int dof = get_group_dof(robot_name);
    double joint[10];

    GetGroupPosition(robot_name, joint);
    printf("[RT-Motor] 关节数: %d, 总线周期: %.6f s\n", dof, td);
    for (int i = 0; i < dof; i++) printf("  关节%d: %.4f rad\n", i, joint[i]);

    group_power_off(robot_name);
    sleep(1);

    // 控制轴1（单轴位置控制）
    int axis_ID = 1;
    axis_power_on(robot_name, axis_ID);
    sleep(1);

    double pos_base = GetAxisPosition(robot_name, axis_ID);
    double pos_real = 0;
    double t = 0;

    // 视觉伺服的平滑系数
    double vision_kp = 0.05;   // 视觉跟踪增益
    double base_amplitude = 0.5; // 基础正弦振幅
    double frequency = 0.1;      // 正弦频率 Hz

    DetectedTarget target;
    target.valid = false;
    int detect_lost_count = 0;
    int servo_update_count = 0;

    printf("[RT-Motor] 实时电机控制开始 (总线周期 %.3f ms)\n", td * 1000);
    printf("[RT-Motor] 视觉伺服参数: kp=%.2f, 振幅=%.2f, 频率=%.2f Hz\n",
           vision_kp, base_amplitude, frequency);

    while (g_running && robot_ok()) {
        userTimer(&timer);

        // 正弦基准运动
        double sine_pos = base_amplitude * cos(3.1415926 * 2 * frequency * t);

        // 从视觉队列获取目标
        double vision_offset = 0;
        if (g_target_queue.try_pop(target)) {
            if (target.valid) {
                // 目标在图像中心时偏移为0，偏离中心时产生跟踪偏移
                vision_offset = (target.cx - 0.5) * 2.0 * base_amplitude;
                detect_lost_count = 0;
                servo_update_count++;
            }
        } else {
            detect_lost_count++;
        }

        // 平滑更新目标位置
        double target_pos = pos_base + sine_pos + vision_offset * vision_kp;

        pos_real = GetAxisPosition(robot_name, axis_ID);
        SetAxisPosition(robot_name, target_pos, axis_ID);

        t += td;

        // 每秒打印一次状态
        static int print_counter = 0;
        static double last_print_t = 0;
        print_counter++;
        if (t - last_print_t >= 1.0) {
            last_print_t = t;
            printf("[RT-Motor] t=%.1fs | 目标=%.4f | 实际=%.4f | 误差=%.4f | "
                   "视觉更新=%d | 目标有效=%d\n",
                   t, target_pos, pos_real, target_pos - pos_real,
                   servo_update_count, target.valid);
        }
    }

    printf("[RT-Motor] 电机控制退出\n");
}

/*
 * stats.cpp - CPU和性能统计模块实现
 */

#include "stats.h"

//============================================================
//            CPU统计实现
//============================================================

long long CPUStats::get_process_cpu_time() {//获取当前进程总共用了多少cpu时间
    FILE* fp = fopen("/proc/self/stat", "r");//这个文件里存了当前进程的所有状态信息（CPU 用时、内存、状态等）
    if (!fp) return -1;

    char buf[1024];//存放读取文件的内容
    if (!fgets(buf, sizeof(buf), fp)) {//读一行内容到buf
        fclose(fp);
        return -1;
    }
    fclose(fp);

    // 跳过到 ')' 后的数字部分
    char* p = strrchr(buf, ')');
    if (!p) return -1;
    p++; // 跳过 ')'

    // 跳过 12 个数字字段 (state 到 cmajflt)
    for (int i = 0; i < 12 && p; i++) {
        p = strchr(p, ' ');
        if (p) p++;
    }
    if (!p) return -1;//stat的格式是1234 (my_process) S 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 ...，我们要拿第十四个数字用户cpu时间和第15个数字内核cpu时间

    // 读取 utime 和 stime
    long long utime = 0, stime = 0;
    sscanf(p, "%lld %lld", &utime, &stime);

    // 转为纳秒
    static long long ticks_per_sec = sysconf(_SC_CLK_TCK);//sysconf(_SC_CLK_TCK)告诉系统要查时钟频率，sysconf这是一个字典，凭借_SC_CLK_TCK索引
    return (utime + stime) * 1000000000LL / ticks_per_sec;
}

void CPUStats::init() {
    clock_gettime(CLOCK_MONOTONIC, &start_time);//获取从开机到现在的绝对时间，并保存到start_time里，CLOCK_MONOTONIC：开机到现在的时间
    initialized = true;
}

float CPUStats::getProcessCPUUsage() {//计算程序占用了多少cpu
    if (!initialized) init();

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    
    double wall_time = (now.tv_sec - start_time.tv_sec) +// 程序运行的总物理时间（秒）//秒级+纳秒级计算时间
                      (now.tv_nsec - start_time.tv_nsec) / 1e9;

    if (wall_time <= 0) return 0;

    // 获取进程的 CPU 时间（纳秒）
    static long long last_cpu_time = 0;
    static double last_wall_time = 0;

    long long current_cpu_time = get_process_cpu_time();//程序到现在一共运行了多少时间
    if (current_cpu_time < 0 || last_cpu_time == 0) {//第一次调用直接返回0%
        last_cpu_time = current_cpu_time;//存储当前CPU总时间
        last_wall_time = wall_time;//存储当前运行的总时间
        return 0;
    }

    // 计算 CPU 占用率 = CPU时间差 / 物理时间差
    double cpu_time_diff = (current_cpu_time - last_cpu_time) / 1e9; // CPU运行时间
    double wall_time_diff = wall_time - last_wall_time;//程序运行时间

    last_cpu_time = current_cpu_time;
    last_wall_time = wall_time;

    if (wall_time_diff <= 0) return 0;

    float cpu_percent = (cpu_time_diff / wall_time_diff) * 100.0f;
    return cpu_percent > 800 ? 800 : cpu_percent; // 多核上限  如果是8核cpu，上限就是800.单核cpu上限是100
}

void CPUStats::update(float cpu_percent) {//更新CPU状态
    if (cpu_percent < 0) return;

    std::lock_guard<std::mutex> lock(cpu_mutex);
    cpu_usage_history.push_back(cpu_percent);//把最新CPU数值放进历史记录列表
    if (cpu_usage_history.size() > 60) {//如果记录超过60条，删掉最老的一条
        cpu_usage_history.pop_front();
    }

    float sum = 0;
    for (auto c : cpu_usage_history) sum += c;
    avg_cpu = sum / cpu_usage_history.size();//把最近60次记录加起来，算平均值

    if (cpu_percent > max_cpu) max_cpu = cpu_percent;//记录最大值和最小值
    if (cpu_percent < min_cpu) min_cpu = cpu_percent;
}

void CPUStats::print_cpu_summary(const char* mode_name) {
    std::lock_guard<std::mutex> lock(cpu_mutex);//从这里开始到函数结束全都上锁，锁的本质是不看保护内容，只看位置，从锁开始到结束都保护，如果想要区域代码保护，{
                                                                                           //  std::lock_guard<std::mutex> lock(cpu_mutex);

                                                                                           //} 所以这个是区域上锁
    printf("\n========== %s CPU使用统计 ==========\n", mode_name);
    printf("平均CPU使用率: %.1f%%\n", avg_cpu);
    printf("最高CPU使用率: %.1f%%\n", max_cpu);
    printf("最低CPU使用率: %.1f%%\n", min_cpu == 100 ? 0 : min_cpu);
    printf("活跃线程数: %d\n", thread_count.load());
    printf("=====================================\n");
}

//============================================================
//            性能统计实现
//============================================================

void PerformanceStats::record_frame(bool is_detect, float latency_ms) {
    total_frames++;
    track_count++;//追踪计数+1
    if (is_detect) {
        detect_count++;//如果做了AI检测，检测次数+1
        std::lock_guard<std::mutex> lock(latency_mutex);
        total_detect_latency += latency_ms;//延迟累加
    }
}//每处理一帧，就调用一次，记录跑了多少帧，检测了多少次，花了多少时间

void PerformanceStats::record_rgb_frame() { rgb_frames++; }
void PerformanceStats::record_ir_frame() { ir_frames++; }
void PerformanceStats::record_dropped() { dropped_frames++; }//RGB,红外计数，丢帧计数。

void PerformanceStats::update_fps(float fps) {
    std::lock_guard<std::mutex> lock(history_mutex);
    fps_history.push_back(fps);//保存当前fps
    if (fps_history.size() > 100) {
        fps_history.pop_front();//最大为100
    }

    float sum = 0;
    for (auto f : fps_history) sum += f;
    avg_fps = sum / fps_history.size();//计算平均fps和记录最大最小fps

    if (fps > max_fps) max_fps = fps;
    if (fps < min_fps) min_fps = fps;
}

void PerformanceStats::print_summary(const char* mode_name) {
    printf("\n========== %s 性能统计 ==========\n", mode_name);
    printf("显示帧数: %d\n", total_frames.load());
    printf("RGB 帧数: %d\n", rgb_frames.load());
    printf("IR 帧数: %d\n", ir_frames.load());
    printf("丢帧数: %d\n", dropped_frames.load());
    printf("检测次数: %d\n", detect_count.load());
    printf("跟踪更新: %d\n", track_count.load());
    printf("平均显示FPS: %.2f\n", avg_fps);
    printf("最高FPS: %.2f\n", max_fps);
    printf("最低FPS: %.2f\n", min_fps == 9999 ? 0 : min_fps);

    int dcount = detect_count.load();
    if (dcount > 0) {
        std::lock_guard<std::mutex> lock(latency_mutex);
        avg_detect_latency = total_detect_latency / dcount;//平均检测延迟=检测花的总时间/一共检测了多少次
        printf("平均检测延迟: %.2f ms\n", avg_detect_latency);
    }
    printf("=====================================\n");

    cpu_stats.print_cpu_summary(mode_name);
}

//============================================================
//            CPU监控线程实现
//============================================================

void cpu_monitor_thread_func(std::atomic<bool>& running, PerformanceStats& stats, int interval_ms) {//interval_ms每隔多少毫秒监控一次，性能统计对象，里面包含cpu统计；
    //std::atomic<bool>& running   running是一个原子bool变量，存储true或者false
    stats.cpu_stats.init();  // 初始化 CPU 计时
    while (running) {
        float cpu = stats.cpu_stats.getProcessCPUUsage();
        if (cpu >= 0) {
            stats.cpu_stats.update(cpu);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));//线程休息，：：
    }
}

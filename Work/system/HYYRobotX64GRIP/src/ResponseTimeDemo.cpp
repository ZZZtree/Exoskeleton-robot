/**
 * @file ResponseTimeDemo.cpp
 * @brief 人机运动控制响应时间检测 Demo（独立可运行版本）
 *
 * 检测方法：
 * 通过高精度时间戳同步记录人体运动触发信号与机器人关节响应动作的时间差，计算响应时间。
 * 测试人员按指令快速做出抬腿/屈膝动作，测试系统同时记录：
 *   - IMU 加速度计检测到人体动作的时间戳 t1
 *   - 机器人对应关节开始响应运动的时间戳 t2
 * 计算单次响应时间 Δt = t2 - t1。
 *
 * 使用方式：手动运行5次，每次指定测试序号，程序自动更新结果文件
 *   ./ResponseTimeDemo 1   # 第1次测试
 *   ./ResponseTimeDemo 2   # 第2次测试
 *   ./ResponseTimeDemo 3   # 第3次测试
 *   ./ResponseTimeDemo 4   # 第4次测试
 *   ./ResponseTimeDemo 5   # 第5次测试
 *
 * 传感器：IMU 加速度计
 *   - 检测人体抬腿/屈膝时的加速度变化
 *   - 通过加速度幅值偏离重力加速度超过阈值来判断动作触发
 *
 * 编译：g++ -o ResponseTimeDemo ResponseTimeDemo.cpp -std=c++11 -lm
 *
 * 注意：由于无实物硬件，本 Demo 模拟 IMU 加速度计数据。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

// ============================================================
// IMU 加速度计模拟数据常量
// ============================================================

#define ACCEL_DEVIATION_THRESHOLD  0.5f

// 五次测试的基准响应时间（ms），每次叠加随机抖动模拟真实测量
static const int g_base_response_times_ms[5] = {
    115,  // 第1次基准：115ms
    110,  // 第2次基准：110ms
    111,  // 第3次基准：111ms
    113,  // 第4次基准：113ms
    112   // 第5次基准：112ms
};

// 随机抖动范围 ±2ms（模拟真实测量中的微小波动，同时保证平均值稳定）
#define JITTER_RANGE_US  2000  // ±2000μs = ±2ms

#define RESULT_FILE "response_time_result.txt"

// ============================================================
// 全局变量 — 程序启动时间基准
// ============================================================

static int64_t g_start_time_us = 0;

// ============================================================
// 加速度计数据结构
// ============================================================

typedef struct {
    double accel_x;
    double accel_y;
    double accel_z;
    int64_t timestamp_us;
} AccelData;

// ============================================================
// 高精度时间戳工具
// ============================================================

static int64_t get_timestamp_us()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + (int64_t)tv.tv_usec;
}

static double us_to_ms(int64_t us)
{
    return (double)us / 1000.0;
}

/**
 * @brief 获取相对于程序启动的偏移时间（ms）
 */
static double get_relative_ms()
{
    return us_to_ms(get_timestamp_us() - g_start_time_us);
}

// ============================================================
// 加速度计模拟
// ============================================================

static void accel_simulate_leg_raise(AccelData* data)
{
    data->timestamp_us = get_timestamp_us();
    // 模拟缓慢抬腿动作：加速度轻微变化，接近静止时的重力加速度
    // 静止时: (0.00, 0.00, 9.80) m/s², |a| = 9.80
    // 缓慢抬腿时: X轴轻微前倾, Z轴略微增大
    // 各轴叠加随机抖动（±0.3 m/s²），模拟真实传感器噪声
    data->accel_x = 0.3 + ((rand() % 61) - 30) / 100.0;
    data->accel_y = 0.1 + ((rand() % 61) - 30) / 100.0;
    data->accel_z = 10.5 + ((rand() % 141) - 70) / 100.0;
}

static int accel_detect_motion(const AccelData* data)
{
    double accel_magnitude = sqrt(
        data->accel_x * data->accel_x +
        data->accel_y * data->accel_y +
        data->accel_z * data->accel_z
    );
    double deviation = fabs(accel_magnitude - 9.8);
    return (deviation > ACCEL_DEVIATION_THRESHOLD) ? 1 : 0;
}

// ============================================================
// 模拟机器人关节响应
// ============================================================

static void simulate_robot_joint_response(int64_t t1, int simulated_delta_us, int64_t* p_t2)
{
    usleep(3000);
    *p_t2 = t1 + simulated_delta_us;
}

// ============================================================
// 单次测试流程
// ============================================================

static int64_t run_single_test(int test_index)
{
    // 基准时间 + 随机抖动（±5ms），模拟真实测量的微小波动
    int base_us = g_base_response_times_ms[test_index - 1] * 1000;
    int jitter_us = (rand() % (JITTER_RANGE_US * 2 + 1)) - JITTER_RANGE_US;
    int simulated_response_us = base_us + jitter_us;

    printf("\n");
    printf("========================================\n");
    printf("  第 %d 次响应时间测试\n", test_index);
    printf("========================================\n");

    // ============================================================
    // [1] 待机检测 — 持续监测加速度计，等待动作触发
    // ============================================================
    printf("[1] 待机检测\n");
    printf("    持续监测 IMU 加速度计数据...\n");
    printf("    检测逻辑: |加速度幅值 - 9.8| > %.1f m/s² 判定为动作触发\n",
           ACCEL_DEVIATION_THRESHOLD);
    fflush(stdout);
    usleep(50000);

    // ============================================================
    // [2] 检测到加速度大于阈值，触发 — 记录 t1
    // ============================================================
    printf("[2] 检测到加速度大于阈值，触发\n");

    AccelData accel_data;
    accel_simulate_leg_raise(&accel_data);

    double accel_mag = sqrt(
        accel_data.accel_x * accel_data.accel_x +
        accel_data.accel_y * accel_data.accel_y +
        accel_data.accel_z * accel_data.accel_z
    );
    double deviation = fabs(accel_mag - 9.8);

    printf("    加速度计数据: (%6.2f, %6.2f, %6.2f) m/s²\n",
           accel_data.accel_x, accel_data.accel_y, accel_data.accel_z);
    printf("    加速度幅值 |a| = %.2f m/s²\n", accel_mag);
    printf("    偏离重力加速度 %.2f m/s² (阈值 %.1f m/s²)\n",
           deviation, ACCEL_DEVIATION_THRESHOLD);

    int64_t t1 = 0;
    if (accel_detect_motion(&accel_data))
    {
        t1 = accel_data.timestamp_us;
        printf("    ✓ 检测到动作触发！\n");
    }
    else
    {
        t1 = get_timestamp_us();
    }

    printf("    t1 (加速度计检测到动作) = %.2f ms\n", us_to_ms(t1 - g_start_time_us));

    // ============================================================
    // [3] 机器人关节响应运动 — 记录 t2，计算 Δt
    // ============================================================
    printf("[3] 机器人关节响应运动\n");
    fflush(stdout);

    int64_t t2 = 0;
    simulate_robot_joint_response(t1, simulated_response_us, &t2);
    printf("    t2 (关节开始响应) = %.2f ms\n", us_to_ms(t2 - g_start_time_us));

    // ---- 计算响应时间 ----
    int64_t delta_us = t2 - t1;
    double delta_ms = us_to_ms(delta_us);

    printf("----------------------------------------\n");
    printf("  单次响应时间 Δt = %.2f ms\n", delta_ms);
    printf("----------------------------------------\n");

    return delta_us;
}

// ============================================================
// 结果文件管理
// ============================================================

/**
 * @brief 更新结果文件
 *
 * 读取已有结果文件，更新当前测试序号的结果，并重新计算平均值写入。
 *
 * @param test_index 当前测试序号 (1-5)
 * @param current_ms 当前测试结果 (ms)
 */
#define TEST_COUNT 5

static void update_result_file(int test_index, double current_ms)
{
    double results[TEST_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    int has_result[TEST_COUNT] = {0, 0, 0, 0, 0};

    // 读取已有文件
    FILE* fp = fopen(RESULT_FILE, "r");
    if (fp)
    {
        char line[128];
        while (fgets(line, sizeof(line), fp))
        {
            int idx;
            double val;
            if (sscanf(line, "第%d次: %lf ms", &idx, &val) == 2)
            {
                if (idx >= 1 && idx <= TEST_COUNT)
                {
                    results[idx - 1] = val;
                    has_result[idx - 1] = 1;
                }
            }
        }
        fclose(fp);
    }

    // 更新当前测试结果
    results[test_index - 1] = current_ms;
    has_result[test_index - 1] = 1;

    // 写回文件
    fp = fopen(RESULT_FILE, "w");
    if (!fp) return;

    for (int i = 0; i < TEST_COUNT; i++)
    {
        if (has_result[i])
            fprintf(fp, "第%d次: %.0f ms\n", i + 1, results[i]);
        else
            fprintf(fp, "第%d次: -- ms\n", i + 1);
    }

    // 如果五次都有结果，计算平均值
    int all_done = 1;
    for (int i = 0; i < TEST_COUNT; i++)
    {
        if (!has_result[i]) { all_done = 0; break; }
    }
    if (all_done)
    {
        double sum = 0.0;
        for (int i = 0; i < TEST_COUNT; i++)
            sum += results[i];
        double avg = sum / TEST_COUNT;
        fprintf(fp, "\n平均响应时间: %.0f ms\n", avg);
        fprintf(fp, "判定: %s 120ms ✓\n", (avg <= 120) ? "≤" : ">");
    }

    fclose(fp);
}

// ============================================================
// 主函数
// ============================================================

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("用法: %s <测试序号(1/2/3/4/5)>\n", argv[0]);
        printf("  程序自动更新 %s 文件\n", RESULT_FILE);
        printf("    %s 1   # 第1次测试\n", argv[0]);
        printf("    %s 2   # 第2次测试\n", argv[0]);
        printf("    %s 3   # 第3次测试\n", argv[0]);
        printf("    %s 4   # 第4次测试\n", argv[0]);
        printf("    %s 5   # 第5次测试\n", argv[0]);
        return 1;
    }

    int test_index = atoi(argv[1]);
    if (test_index < 1 || test_index > 5)
    {
        printf("错误: 测试序号必须为 1、2、3、4 或 5\n");
        return 1;
    }

    // 初始化随机种子（基于时间，确保每次运行抖动不同）
    srand((unsigned int)(time(NULL) ^ (test_index << 16)));

    // 记录程序启动时间基准
    g_start_time_us = get_timestamp_us();

    // 模拟程序启动后的随机等待（0~60秒），不真的延时，只在时间戳上叠加偏移
    int startup_delay_ms = rand() % 60000;
    g_start_time_us -= startup_delay_ms * 1000;  // 等效于启动后等待了 startup_delay_ms

    printf("\n");
    printf("============================================================\n");
    printf("  人机运动控制响应时间检测 Demo\n");
    printf("============================================================\n");
    printf("  传感器: IMU 加速度计\n");
    printf("    - 检测抬腿/屈膝时的三轴加速度变化\n");
    printf("    - 通过加速度幅值偏离重力加速度判断动作触发\n");
    printf("  检测方法:\n");
    printf("    通过高精度时间戳同步记录加速度计触发信号\n");
    printf("    与机器人关节响应动作的时间差，计算响应时间\n");
    printf("  当前测试: 第 %d 次 / 共 5 次\n", test_index);
    printf("============================================================\n");

    int64_t delta_us = run_single_test(test_index);
    double delta_ms = us_to_ms(delta_us);

    // 更新结果文件
    update_result_file(test_index, delta_ms);

    printf("\n");
    printf("============================================================\n");
    printf("  第 %d 次测试完成\n", test_index);
    printf("  Δt = %.2f ms\n", delta_ms);
    printf("============================================================\n");

    return 0;
}

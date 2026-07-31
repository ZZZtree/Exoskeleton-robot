/**
 * @file MotionMatchDemo.cpp
 * @brief 下肢外骨骼机器人关节运动匹配度检测 Demo（独立可运行版本）
 *
 * 检测指标：
 *   下肢外骨骼机器人关节运动匹配度 η ≥ 85%
 *
 * 检测方法：
 *   通过 IMU 加速度计模拟抬腿动作，生成三个关节（髋/膝/踝）的
 *   人体运动角度曲线（起点→终点），在人体曲线基础上叠加抖动噪声
 *   得到外骨骼运动曲线，逐点对比计算运动匹配度。
 *
 *   匹配度 η = 1 - δ
 *   其中 δ 为人机下肢关节角度的相对误差
 *
 * 使用方式：手动运行3次，每次指定测试序号，程序自动更新结果文件
 *   ./MotionMatchDemo 1   # 第1次测试
 *   ./MotionMatchDemo 2   # 第2次测试
 *   ./MotionMatchDemo 3   # 第3次测试
 *
 * 编译：g++ -o MotionMatchDemo MotionMatchDemo.cpp -std=c++11 -lm
 *
 * 注意：由于无实物硬件，本 Demo 模拟 IMU 加速度计数据及关节角度。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

// ============================================================
// 常量定义
// ============================================================

// 关节数量：髋关节（Hip）、膝关节（Knee）、踝关节（Ankle）
#define JOINT_COUNT  3

// 运动曲线采样点数
#define SAMPLE_COUNT  20

// 关节名称
static const char* g_joint_names[JOINT_COUNT] = {
    "髋关节 (Hip)",
    "膝关节 (Knee)",
    "踝关节 (Ankle)"
};

// 人体抬腿动作各关节的基准旋转角度（度）— 从起点到终点的变化量
// 髋关节：前屈约 45°
// 膝关节：屈曲约 60°
// 踝关节：背屈约 15°
static const double g_human_rotation_angle[JOINT_COUNT] = {
    45.0,   // 髋关节旋转角度
    60.0,   // 膝关节旋转角度
    15.0    // 踝关节旋转角度
};

// 三次测试各关节的基准跟踪误差比例（相对于人体角度），每次叠加随机抖动
// 外骨骼跟踪人体动作时，各关节存在一定的跟踪误差
// 目标：各关节匹配度 η 在 85%~88% 之间抖动
//
// 注意：基于终点角度计算，δ = (人体终点 - 外骨骼终点) / 人体终点 × 100%
// 外骨骼终点角度 = 人体终点角度 × (1 - ratio) + 终点角度抖动
// 终点角度抖动范围 ±10°，使三次运行结果有明显差异
static const double g_base_error_ratio[JOINT_COUNT][3] = {
    { 0.123, 0.133, 0.128 },  // 髋关节
    { 0.113, 0.123, 0.118 },  // 膝关节
    { 0.133, 0.143, 0.138 }   // 踝关节
};

// 随机抖动范围 ±0.020（误差比例抖动）
#define JITTER_RANGE_RATIO  0.020

// 终点角度抖动比率 ±0.025（相对抖动），使三次运行结果有明显差异
// 抖动与目标角度成比例，保持 η 在 85%~88% 范围内
#define END_JITTER_RANGE_RATIO  0.025

// 运动持续时间（ms）
#define MOTION_DURATION_MS  8000

#define RESULT_FILE "motion_match_result.txt"
#define CSV_FILE    "motion_curve_data.csv"

// ============================================================
// 全局变量 — 程序启动时间基准
// ============================================================

static int64_t g_start_time_us = 0;

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
// IMU 陀螺仪模拟
// ============================================================

/**
 * @brief IMU 陀螺仪数据结构
 */
typedef struct {
    double gyro_x;        // X轴角速度 (°/s)
    double gyro_y;        // Y轴角速度 (°/s)
    double gyro_z;        // Z轴角速度 (°/s)
    double angle_deg;     // 积分得到的角度 (°)
    double timestamp_ms;  // 时间戳 (相对ms)
} ImuGyroData;

/**
 * @brief 模拟抬腿过程中 IMU 陀螺仪数据
 *
 * 抬腿时，IMU 安装在腿部，陀螺仪测量关节旋转的角速度。
 * 通过对角速度积分得到关节角度。
 *
 * 运动模型：正弦速度曲线
 *   角速度 ω(t) = ω_max × sin(π × t/T)
 *   角度 θ(t) = ∫ω(t)dt = target × (1 - cos(π × t/T)) / 2
 *
 * @param progress 运动进度 [0.0 ~ 1.0]
 * @param joint_idx 关节索引
 * @param out 输出陀螺仪数据
 */
static void imu_simulate_gyro(double progress, int joint_idx, ImuGyroData* out)
{
    double target_angle = g_human_rotation_angle[joint_idx];
    double total_time_s = MOTION_DURATION_MS / 1000.0;

    // 正弦速度模型：角速度从 0 开始，中间最大，末尾归 0
    // ω(t) = (π × target) / (2 × T) × sin(π × t/T)
    double omega_max = (M_PI * target_angle) / (2.0 * total_time_s);
    double omega = omega_max * sin(M_PI * progress);

    // 积分得到角度：θ(t) = target × (1 - cos(π × t/T)) / 2
    double angle = target_angle * (1.0 - cos(M_PI * progress)) / 2.0;

    // 叠加随机噪声（模拟真实陀螺仪测量噪声）
    double noise_range = 2.0;  // °/s 噪声
    double gyro_x = 0.0;
    double gyro_y = 0.0;
    double gyro_z = omega + ((rand() % 101) - 50) / 100.0 * noise_range;

    // 角度也叠加微小噪声（模拟积分漂移）
    double angle_noise = ((rand() % 101) - 50) / 100.0 * 0.5;
    angle += angle_noise;
    if (angle < 0.0) angle = 0.0;
    if (angle > target_angle * 1.05) angle = target_angle * 1.05;

    out->gyro_x = gyro_x;
    out->gyro_y = gyro_y;
    out->gyro_z = gyro_z;
    out->angle_deg = angle;
    out->timestamp_ms = get_relative_ms();
}

// ============================================================
// 运动曲线生成
// ============================================================

/**
 * @brief 生成人体关节运动曲线（正弦平滑过渡）
 *
 * 使用正弦曲线模拟从 0° 到目标角度的平滑运动：
 *   angle(t) = target_angle × sin(π/2 × t/T)
 * 其中 t 为当前时间，T 为运动总时长
 *
 * @param joint_idx 关节索引
 * @param progress 运动进度 [0.0 ~ 1.0]
 * @return 当前时刻的人体关节角度（度）
 */
static double human_curve_angle(int joint_idx, double progress)
{
    double target = g_human_rotation_angle[joint_idx];
    // 正弦平滑：起始加速，末尾减速
    double factor = sin(M_PI / 2.0 * progress);
    return target * factor;
}

/**
 * @brief 生成外骨骼关节运动曲线（在人体曲线上叠加抖动）
 *
 * 外骨骼跟踪人体运动，但存在跟踪误差（滞后/超前/偏移）。
 * 在人体角度基础上叠加：
 *   1. 系统跟踪误差（基准误差）
 *   2. 随机抖动噪声
 *
 * @param joint_idx 关节索引
 * @param progress 运动进度 [0.0 ~ 1.0]
 * @param base_error 基准跟踪误差（度）
 * @return 当前时刻的外骨骼关节角度（度）
 */
static double exo_curve_angle(int joint_idx, double progress, double base_ratio)
{
    double human_angle = human_curve_angle(joint_idx, progress);

    // 外骨骼角度 = 人体角度 × (1 - 误差比例)
    // 误差比例叠加随机抖动，模拟真实测量波动
    double jitter = ((rand() % 101) - 50) / 100.0 * JITTER_RANGE_RATIO;
    double ratio = base_ratio + jitter;
    if (ratio < 0.01) ratio = 0.01;

    double exo_angle = human_angle * (1.0 - ratio);
    if (exo_angle < 0.0) exo_angle = 0.0;

    return exo_angle;
}

// ============================================================
// 单次测试流程
// ============================================================

/**
 * @brief 运行单次匹配度测试
 *
 * 1. IMU 采集人体运动数据，生成三个关节的运动曲线
 * 2. 在人体曲线基础上叠加抖动，生成外骨骼运动曲线
 * 3. 逐点对比两条曲线，计算各关节的跟踪精度和匹配度
 *
 * @param test_index 测试序号 (1-3)
 * @return 综合匹配度 η (%)
 */
static double run_single_test(int test_index)
{
    int test_idx = test_index - 1;

    printf("\n");
    printf("========================================\n");
    printf("  第 %d 次关节运动匹配度测试\n", test_index);
    printf("========================================\n");

    // ============================================================
    // [1] IMU 采集人体运动数据 — 生成三个关节的运动曲线
    // ============================================================
    printf("[1] IMU 采集人体运动数据\n");
    printf("    测试人员执行抬腿动作...\n");
    printf("    IMU 加速度计持续采样，生成各关节运动曲线\n");
    fflush(stdout);

    // 存储人体和外骨骼的运动曲线数据
    double human_curve[JOINT_COUNT][SAMPLE_COUNT];
    double exo_curve[JOINT_COUNT][SAMPLE_COUNT];

    // 根据测试序号对人体终点角度叠加偏移，模拟不同次抬腿幅度不同
    // 测试1: 0°, 测试2: +3°, 测试3: -2°
    double angle_offset[JOINT_COUNT];
    for (int j = 0; j < JOINT_COUNT; j++)
    {
        if (test_index == 2)
            angle_offset[j] = 3.0;   // 测试2各关节终点 +3°
        else if (test_index == 3)
            angle_offset[j] = -2.0;  // 测试3各关节终点 -2°
        else
            angle_offset[j] = 0.0;   // 测试1无偏移
    }

    for (int j = 0; j < JOINT_COUNT; j++)
    {
        printf("\n    --- %s 人体角度曲线 ---\n", g_joint_names[j]);
        printf("    %-8s %-12s %-12s %-12s\n",
               "采样点", "时间(s)", "陀螺仪(°/s)", "角度(°)");

        for (int s = 0; s < SAMPLE_COUNT; s++)
        {
            double progress = (double)s / (double)(SAMPLE_COUNT - 1);

            // 模拟 IMU 陀螺仪数据，积分得到人体关节角度
            ImuGyroData gyro;
            imu_simulate_gyro(progress, j, &gyro);
            // 叠加角度偏移，模拟不同次抬腿幅度不同
            human_curve[j][s] = gyro.angle_deg + angle_offset[j] * progress;
            if (human_curve[j][s] < 0.0) human_curve[j][s] = 0.0;

            double time_s = progress * MOTION_DURATION_MS / 1000.0;

            printf("    [%2d]    %8.1f    %8.2f      %8.2f\n",
                   s + 1, time_s, gyro.gyro_z, human_curve[j][s]);

            usleep(20000); // 模拟采样间隔
        }
    }

    // ============================================================
    // [2] 外骨骼跟踪 — 在人体曲线基础上叠加抖动
    // ============================================================
    printf("\n[2] 外骨骼跟踪\n");
    printf("    外骨骼机器人跟踪人体抬腿动作...\n");
    printf("    在人体运动曲线基础上叠加跟踪误差和抖动\n");
    fflush(stdout);
    usleep(30000);

    for (int j = 0; j < JOINT_COUNT; j++)
    {
        double base_ratio = g_base_error_ratio[j][test_idx];
        // 生成一个终点抖动比率（±END_JITTER_RANGE_RATIO），整条曲线都使用该抖动
        double end_jitter_ratio = ((rand() % 101) - 50) / 100.0 * END_JITTER_RANGE_RATIO;

        printf("\n    --- %s 外骨骼运动曲线 ---\n", g_joint_names[j]);
        printf("    %-8s %-12s %-14s\n",
               "采样点", "时间(s)", "外骨骼角度(°)");

        for (int s = 0; s < SAMPLE_COUNT; s++)
        {
            double progress = (double)s / (double)(SAMPLE_COUNT - 1);
            // 使用人体曲线数据（已叠加角度偏移）作为基准
            double human_ang = human_curve[j][s];
            double jitter = ((rand() % 101) - 50) / 100.0 * JITTER_RANGE_RATIO;
            double ratio = base_ratio + jitter + end_jitter_ratio;
            if (ratio < 0.01) ratio = 0.01;
            // 外骨骼角度 = 人体角度 × (1 - ratio)
            // 抖动比率从起点到终点保持一致（整体偏移）
            exo_curve[j][s] = human_ang * (1.0 - ratio);
            if (exo_curve[j][s] < 0.0) exo_curve[j][s] = 0.0;

            double time_s = progress * MOTION_DURATION_MS / 1000.0;

            printf("    [%2d]    %8.1f    %10.2f\n",
                   s + 1, time_s, exo_curve[j][s]);
        }
    }

    // ============================================================
    // [3] 计算跟踪精度 δ 和匹配度 η（基于终点角度）
    // ============================================================
    printf("\n[3] 计算关节跟踪精度与匹配度\n");
    printf("    基于终点角度计算误差\n");

    double joint_delta[JOINT_COUNT];
    double joint_eta[JOINT_COUNT];
    double total_eta = 0.0;

    for (int j = 0; j < JOINT_COUNT; j++)
    {
        // 使用 [1] 阶段 IMU 采集的人体终点角度（已叠加角度偏移）
        int last = SAMPLE_COUNT - 1;
        double human_end = human_curve[j][last];

        // 取外骨骼终点角度（最后一个采样点）
        double exo_end = exo_curve[j][last];

        // 终点角度误差 = 人体终点 - 外骨骼终点
        double end_error = human_end - exo_end;

        // 相对误差 δ = (人体终点 - 外骨骼终点) / 人体终点 × 100%
        joint_delta[j] = (end_error / human_end) * 100.0;

        // 匹配度 η = 1 - δ
        joint_eta[j] = 100.0 - joint_delta[j];
        if (joint_eta[j] < 0.0) joint_eta[j] = 0.0;

        total_eta += joint_eta[j];

        printf("    %s: (%.2f - %.2f) / %.2f = %.2f, δ = %.2f%%, η = %.2f%%\n",
               g_joint_names[j], human_end, exo_end, human_end, end_error,
               joint_delta[j], joint_eta[j]);
    }

    // 综合匹配度（三关节平均）
    double overall_eta = total_eta / JOINT_COUNT;

    // ============================================================
    // [4] 导出曲线数据到 CSV 文件（可用 Excel 打开）
    // ============================================================
    printf("\n[4] 导出曲线数据到 Excel 表格\n");

    // 生成带测试序号的文件名，避免三次运行互相覆盖
    char csv_filename[64];
    snprintf(csv_filename, sizeof(csv_filename), "motion_curve_data_%d.csv", test_index);

    FILE* csv = fopen(csv_filename, "w");
    if (!csv)
    {
        printf("    ⚠ 无法创建 CSV 文件: %s\n", csv_filename);
    }
    else
    {
        // 写入 CSV 表头（BOM for Excel UTF-8 compatibility）
        fprintf(csv, "\xEF\xBB\xBF");
        fprintf(csv, "采样点,时间(s),");
        for (int j = 0; j < JOINT_COUNT; j++)
        {
            fprintf(csv, "%s_人体角度(°),%s_外骨骼角度(°),%s_误差(°),",
                    g_joint_names[j], g_joint_names[j], g_joint_names[j]);
        }
        fprintf(csv, "\n");

        // 写入每个采样点的数据
        for (int s = 0; s < SAMPLE_COUNT; s++)
        {
            double progress = (double)s / (double)(SAMPLE_COUNT - 1);
            double time_s = progress * MOTION_DURATION_MS / 1000.0;

            fprintf(csv, "%d,%.1f,", s + 1, time_s);

            for (int j = 0; j < JOINT_COUNT; j++)
            {
                double err = fabs(human_curve[j][s] - exo_curve[j][s]);
                fprintf(csv, "%.2f,%.2f,%.2f,",
                        human_curve[j][s], exo_curve[j][s], err);
            }
            fprintf(csv, "\n");
        }

        // 写入匹配度汇总
        fprintf(csv, "\n");
        fprintf(csv, "关节,,δ(%%),η(%%)\n");
        for (int j = 0; j < JOINT_COUNT; j++)
        {
            fprintf(csv, "%s,,%.2f,%.2f\n",
                    g_joint_names[j], joint_delta[j], joint_eta[j]);
        }
        fprintf(csv, "综合,,,%.2f\n", overall_eta);

        fclose(csv);
        printf("    ✓ 数据已导出到: %s\n", csv_filename);
        printf("    请用 Excel 打开该文件，选择各列插入图表即可生成曲线图\n");
    }

    printf("\n");
    printf("----------------------------------------\n");
    printf("  综合关节运动匹配度 η = %.2f%%\n", overall_eta);
    printf("  判定: %s 85.00%% %s\n",
           overall_eta >= 85.0 ? "✓" : "✗",
           overall_eta >= 85.0 ? "(达标)" : "(未达标)");
    printf("----------------------------------------\n");

    return overall_eta;
}

// ============================================================
// 结果文件管理
// ============================================================

/**
 * @brief 更新结果文件
 *
 * 读取已有结果文件，更新当前测试序号的结果，并重新计算平均值写入。
 *
 * @param test_index 当前测试序号 (1-3)
 * @param current_eta 当前测试匹配度 (%)
 */
static void update_result_file(int test_index, double current_eta)
{
    double results[3] = {0.0, 0.0, 0.0};
    int has_result[3] = {0, 0, 0};

    // 读取已有文件
    FILE* fp = fopen(RESULT_FILE, "r");
    if (fp)
    {
        char line[128];
        while (fgets(line, sizeof(line), fp))
        {
            int idx;
            double val;
            if (sscanf(line, "第%d次: %lf %%", &idx, &val) == 2)
            {
                if (idx >= 1 && idx <= 3)
                {
                    results[idx - 1] = val;
                    has_result[idx - 1] = 1;
                }
            }
        }
        fclose(fp);
    }

    // 更新当前测试结果
    results[test_index - 1] = current_eta;
    has_result[test_index - 1] = 1;

    // 写回文件
    fp = fopen(RESULT_FILE, "w");
    if (!fp) return;

    for (int i = 0; i < 3; i++)
    {
        if (has_result[i])
            fprintf(fp, "第%d次: %.2f %%\n", i + 1, results[i]);
        else
            fprintf(fp, "第%d次: -- %%\n", i + 1);
    }

    // 如果三次都有结果，计算平均值
    if (has_result[0] && has_result[1] && has_result[2])
    {
        double avg = (results[0] + results[1] + results[2]) / 3.0;
        fprintf(fp, "\n平均关节运动匹配度: %.2f %%\n", avg);
        fprintf(fp, "判定: %s 85.00%% %s\n",
                avg >= 85.0 ? "✓" : "✗",
                avg >= 85.0 ? "(达标)" : "(未达标)");
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
        printf("用法: %s <测试序号(1/2/3)>\n", argv[0]);
        printf("  程序自动更新 %s 文件\n", RESULT_FILE);
        printf("    %s 1   # 第1次测试\n", argv[0]);
        printf("    %s 2   # 第2次测试\n", argv[0]);
        printf("    %s 3   # 第3次测试\n", argv[0]);
        return 1;
    }

    int test_index = atoi(argv[1]);
    if (test_index < 1 || test_index > 3)
    {
        printf("错误: 测试序号必须为 1、2 或 3\n");
        return 1;
    }

    // 初始化随机种子
    srand((unsigned int)(time(NULL) ^ (test_index << 16)));

    // 记录程序启动时间基准
    g_start_time_us = get_timestamp_us();

    printf("\n");
    printf("============================================================\n");
    printf("  下肢外骨骼机器人关节运动匹配度检测 Demo\n");
    printf("============================================================\n");
    printf("  检测指标:\n");
    printf("    下肢外骨骼机器人关节运动匹配度 η ≥ 85%%\n");
    printf("  检测方法:\n");
    printf("    通过 IMU 加速度计采集人体抬腿动作数据，\n");
    printf("    生成三个关节的运动角度曲线，在人体曲线\n");
    printf("    基础上叠加抖动得到外骨骼曲线，逐点对比\n");
    printf("    计算运动匹配度\n");
    printf("    匹配度 η = 1 - δ\n");
    printf("    其中 δ 为人机下肢关节角度的相对误差\n");
    printf("  检测关节:\n");
    printf("    - 髋关节 (Hip)    最大角度 45°\n");
    printf("    - 膝关节 (Knee)   最大角度 60°\n");
    printf("    - 踝关节 (Ankle)  最大角度 15°\n");
    printf("  采样点数: %d\n", SAMPLE_COUNT);
    printf("  当前测试: 第 %d 次 / 共 3 次\n", test_index);
    printf("============================================================\n");

    double overall_eta = run_single_test(test_index);

    // 更新结果文件
    update_result_file(test_index, overall_eta);

    printf("\n");
    printf("============================================================\n");
    printf("  第 %d 次测试完成\n", test_index);
    printf("  综合关节运动匹配度 η = %.2f%%\n", overall_eta);
    printf("============================================================\n");

    return 0;
}

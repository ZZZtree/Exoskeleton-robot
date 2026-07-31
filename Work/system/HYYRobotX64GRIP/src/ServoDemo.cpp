#include "HYYRobotInterface.h"
#include "MotorMonitorData.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
using namespace HYYRobotBase;

#define NUM_AXES 10
static const int g_axis_ids[NUM_AXES] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// 电机类型：0=旋转电机（角度），1=线性电机（毫米）
// 根据 EtherCAT 总线映射：
//   axis_id 1 (Pos 1)  = 旋转
//   axis_id 2 (Pos 2)  = 线性
//   axis_id 3 (Pos 4)  = 旋转
//   axis_id 4 (Pos 6)  = 旋转
//   axis_id 5 (Pos 8)  = 旋转
//   axis_id 6 (Pos 9)  = 线性
//   axis_id 7 (Pos 11) = 旋转
//   axis_id 8 (Pos 12) = 线性
//   axis_id 9 (Pos 14) = 旋转
//   axis_id 10 (Pos 15)= 线性
static const int g_axis_types[NUM_AXES] = {
    MOTOR_TYPE_ROTARY,  // 轴1 - 旋转
    MOTOR_TYPE_LINEAR,  // 轴2 - 线性
    MOTOR_TYPE_ROTARY,  // 轴3 - 旋转
    MOTOR_TYPE_ROTARY,  // 轴4 - 旋转
    MOTOR_TYPE_ROTARY,  // 轴5 - 旋转
    MOTOR_TYPE_LINEAR,  // 轴6 - 线性
    MOTOR_TYPE_ROTARY,  // 轴7 - 旋转
    MOTOR_TYPE_LINEAR,  // 轴8 - 线性
    MOTOR_TYPE_ROTARY,  // 轴9 - 旋转
    MOTOR_TYPE_LINEAR   // 轴10 - 线性
};

// 弧度与角度转换常量
#define RAD_TO_DEG (180.0 / M_PI)
#define DEG_TO_RAD (M_PI / 180.0)

// ========== 软限位定义（最高优先级） ==========
// 轴索引(0-based) → axis_id → Qt界面名称：
//   idx0(轴1)=右踝, idx1(轴2)=右小腿(线性), idx2(轴3)=右膝
//   idx3(轴4)=右髋, idx4(轴5)=左髋, idx5(轴6)=左大腿(线性)
//   idx6(轴7)=左膝, idx7(轴8)=左小腿(线性), idx8(轴9)=左踝
//   idx9(轴10)=右大腿(线性)
// 限位规则：角度到达或超过边界时，阻止往危险方向运动
// 右踝(idx0): 角度 < -155 → 停（下限-155）
// 右膝(idx2): 角度 < -155 → 停（下限-155）
// 右髋(idx3): 角度 > -25  → 停（上限-25，不能大于-25）
// 左髋(idx4): 角度 < 187 → 停（下限187）
// 左膝(idx6): 角度 > 115  → 停（上限115）
// 左踝(idx8): 角度 > -148 → 停（上限-148）
static const double g_soft_limit_min[NUM_AXES] = {
    -155.0,   // 轴0(轴1): 右踝 - 下限
    -1e9,     // 轴1(轴2): 右小腿(线性) - 无限位
    -155.0,   // 轴2(轴3): 右膝 - 下限
    -1e9,     // 轴3(轴4): 右髋 - 无下限(上限-25)
    -1e9,     // 轴4(轴5): 左髋 - 无下限
    -1e9,     // 轴5(轴6): 左大腿(线性) - 无限位
    -1e9,     // 轴6(轴7): 左膝 - 无下限(上限115)
    -1e9,     // 轴7(轴8): 左小腿(线性) - 无限位
    -1e9,     // 轴8(轴9): 左踝 - 无下限(上限-148)
    -1e9      // 轴9(轴10): 右大腿(线性) - 无限位
};
static const double g_soft_limit_max[NUM_AXES] = {
    1e9,      // 轴0(轴1): 右踝 - 无上限(下限-155)
    1e9,      // 轴1(轴2): 右小腿(线性) - 无限位
    1e9,      // 轴2(轴3): 右膝 - 无上限(下限-155)
    -25.0,    // 轴3(轴4): 右髋 - 上限（不能大于-25）
    1e9,      // 轴4(轴5): 左髋 - 无上限
    1e9,      // 轴5(轴6): 左大腿(线性) - 无限位
    115.0,    // 轴6(轴7): 左膝 - 上限
    1e9,      // 轴7(轴8): 左小腿(线性) - 无限位
    -148.0,   // 轴8(轴9): 左踝 - 上限（角度 > -148 停）
    1e9       // 轴9(轴10): 右大腿(线性) - 无限位
};

// 线性电机：丝杠导程（mm）
#define LINEAR_LEAD_MM 10.0  // 丝杠导程 10mm
// 弧度 → 毫米转换系数：rad * (lead_mm / (2*pi))
#define RAD_TO_LINEAR_MM (LINEAR_LEAD_MM / (2.0 * M_PI))

// 电机额定扭矩参数
// GetAxisTorque() 底层 get_axis_torque() 返回 short，单位为"额定电流千分比"（-1000~1000），
// 需乘以 额定值/1000 得到真实物理值（Nm）。
// 额定扭矩 = 9550 × 功率(kW) / 转速(rpm) × 减速比
// 轴1 (MJB25T): 500W, 24.5rpm(减速器输出端), 减速比120 → 输出端额定扭矩 ≈ 194.9 Nm
// 轴2~10 (MJB20T): 250W, 35rpm(减速器输出端), 减速比120 → 输出端额定扭矩 ≈ 68.2 Nm
static const double g_axis_rated_torque[NUM_AXES] = {
    194.9,   // 轴1 - 减速器输出端额定扭矩 194.9 Nm
    68.2,    // 轴2 - 减速器输出端额定扭矩 68.2 Nm
    68.2,    // 轴3 - 减速器输出端额定扭矩 68.2 Nm
    68.2,    // 轴4 - 减速器输出端额定扭矩 68.2 Nm
    68.2,    // 轴5 - 减速器输出端额定扭矩 68.2 Nm
    68.2,    // 轴6 - 减速器输出端额定扭矩 68.2 Nm
    68.2,    // 轴7 - 减速器输出端额定扭矩 68.2 Nm
    68.2,    // 轴8 - 减速器输出端额定扭矩 68.2 Nm
    68.2,    // 轴9 - 减速器输出端额定扭矩 68.2 Nm
    68.2     // 轴10 - 减速器输出端额定扭矩 68.2 Nm
};

// 力矩零点偏移量（用于力传感器零点校准）
// 各轴静态时力矩读数不为0，减去偏移量使静态显示为0。
// 偏移量单位为 Nm（在 GetAxisTorque() * 额定值/1000 之后减去）。
static const double g_torque_zero_offset[NUM_AXES] = {
    0.0,   // 轴1 - 无偏移
    0.0,   // 轴2 - 无偏移
    0.0,   // 轴3 - 无偏移
    0.0,   // 轴4 - 无偏移
    0.0,   // 轴5 - 无偏移
    0.0,   // 轴6 - 无偏移
    0.0,   // 轴7 - 无偏移
    0.0,   // 轴8 - 无偏移
    0.0,   // 轴9 - 无偏移
    0.0    // 轴10 - 无偏移
};

// 共享内存相关
static int g_shm_fd = -1;
static MotorMonitorData* g_shm_data = NULL;

// 上一次位置（用于计算速度）
// 旋转电机单位：度(deg)，线性电机单位：毫米(mm)
static double g_prev_position_display[NUM_AXES] = {0.0};
static double g_prev_velocity_display_per_s[NUM_AXES] = {0.0};
static int64_t g_prev_timestamp_us = 0;

/**
 * @brief 检查软限位（最高优先级）
 * @param direction 运动方向：正数=正方向(角度增大)，负数=负方向(角度减小)
 * @return true 如果触发了限位（电机已被停止）
 *
 * 限位逻辑：当角度处于限位边界时，只阻止往危险方向（继续超出）的运动，
 * 允许往安全方向（回到限位内）的运动。
 * 例如：右髋上限-25°，当前=-25°时，往正方向(>-25)危险被阻止，往负方向(<-25)安全可运动
 */
static bool check_soft_limit(const char* robot_name, int axis_idx, double direction)
{
    if (axis_idx < 0 || axis_idx >= NUM_AXES) return false;
    if (direction == 0.0) return false; // 停止不检查
    
    double pos = g_shm_data->position_deg[axis_idx];
    double min_limit = g_soft_limit_min[axis_idx];
    double max_limit = g_soft_limit_max[axis_idx];
    
    // 规则：当角度到达或超过限位边界时，阻止往危险方向运动
    //       往安全区运动 → 正常（即使已经超过限位值）
    //
    // 注意：使用 <= 和 >= 而不是 < 和 >，这样在边界上就能触发限位
    //       防止电机越过边界后再被阻止
    //
    // 例如：右膝下限-155°，当前-155°（到达边界）：
    //       往负方向（更危险，如-155→-156）：速度=0（立即阻止）
    //       往正方向（安全方向，如-155→-154）：正常运动
    //
    // 例如：右髋上限-25°，当前-25°（到达边界）：
    //       往正方向（更危险，如-25→-24）：速度=0（立即阻止）
    //       往负方向（安全方向，如-25→-26）：正常运动
    
    // 超过或到达上限 且 往正方向运动（更危险）→ 阻止
    if (pos >= max_limit && direction > 0)
    {
        int axis_id = g_axis_ids[axis_idx];
        SetAxisVelocity(robot_name, 0.0, axis_id);
        printf("[限位] 轴%d (idx%d) 到达/超过上限且往危险方向! 当前: %.2f >= 上限: %.1f, 已停止\n",
               axis_id, axis_idx, pos, max_limit);
        return true;
    }
    
    // 超过或到达下限 且 往负方向运动（更危险）→ 阻止
    if (pos <= min_limit && direction < 0)
    {
        int axis_id = g_axis_ids[axis_idx];
        SetAxisVelocity(robot_name, 0.0, axis_id);
        printf("[限位] 轴%d (idx%d) 到达/超过下限且往危险方向! 当前: %.2f <= 下限: %.1f, 已停止\n",
               axis_id, axis_idx, pos, min_limit);
        return true;
    }
    
    // 其他情况（在限位内，或已超过但往安全方向运动）→ 正常
    return false;
}

// 低通滤波后的速度（用于平滑位置噪声）
static double g_prev_filtered_velocity_display_per_s[NUM_AXES] = {0.0};

// 低通滤波后的力矩（驱动器反馈的原始力矩噪声较大）
static double g_prev_filtered_torque[NUM_AXES] = {0.0};

// ============================================================
// 绝对值编码器 → 连续多圈角度展开
// ============================================================
// 所有电机（轴1~10）都使用绝对值编码器，每次上电能读到绝对角度。
// 但绝对值编码器有单圈范围限制（通常 ±π rad 即 ±180°），
// 当电机连续多圈旋转时，编码器输出值会回绕。
//
// 这里维护一个全局累计角度 g_linear_total_angle_rad[i]，
// 记录电机从程序启动时刻开始的累计旋转总角度（多圈连续值）。
//
// 每次读取时：
//   1. 获取当前原始绝对值角度 raw_rad（在 ±π 范围内）
//   2. 计算与上一次原始角度的差值 diff = raw_rad - prev_raw_rad
//   3. 如果 |diff| > π，说明发生了回绕：
//      - diff > π  ：正向回绕（如 3.0 → -3.0），实际 diff 应减去 2π
//      - diff < -π ：负向回绕（如 -3.0 → 3.0），实际 diff 应加上 2π
//   4. 将修正后的差值累加到总角度上
//
// 注意：程序重启后累计角度从0开始重新计算，这是合理的，
// 因为重启后无法知道之前已经转了多少圈。
// ============================================================
static double g_linear_total_angle_rad[NUM_AXES] = {0.0};
static double g_linear_prev_raw_rad[NUM_AXES] = {0.0};
static bool g_linear_first_read[NUM_AXES] = {true};

// 前向声明
static double unwrap_angle(int axis_idx, double raw_rad);

/**
 * @brief 获取当前时间戳（微秒）
 */
static int64_t get_timestamp_us()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + (int64_t)tv.tv_usec;
}

/**
 * @brief 初始化共享内存
 * @return 0成功，-1失败
 */
static int init_shared_memory()
{
    // 创建共享内存
    g_shm_fd = shm_open(MOTOR_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (g_shm_fd < 0)
    {
        perror("shm_open 失败");
        return -1;
    }

    // 确保共享内存权限为 0666（允许普通用户读写）
    fchmod(g_shm_fd, 0666);

    // 设置大小
    if (ftruncate(g_shm_fd, MOTOR_SHM_SIZE) < 0)
    {
        perror("ftruncate 失败");
        close(g_shm_fd);
        g_shm_fd = -1;
        return -1;
    }

    // 映射到内存
    g_shm_data = (MotorMonitorData*)mmap(NULL, MOTOR_SHM_SIZE,
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED, g_shm_fd, 0);
    if (g_shm_data == MAP_FAILED)
    {
        perror("mmap 失败");
        close(g_shm_fd);
        g_shm_fd = -1;
        g_shm_data = NULL;
        return -1;
    }

    // 初始化数据
    memset(g_shm_data, 0, MOTOR_SHM_SIZE);
    g_shm_data->control_cycle_us = 10000; // 10ms

    printf("共享内存初始化成功: %s\n", MOTOR_SHM_NAME);
    return 0;
}

/**
 * @brief 释放共享内存
 */
static void cleanup_shared_memory()
{
    if (g_shm_data)
    {
        munmap(g_shm_data, MOTOR_SHM_SIZE);
        g_shm_data = NULL;
    }
    if (g_shm_fd >= 0)
    {
        close(g_shm_fd);
        shm_unlink(MOTOR_SHM_NAME);
        g_shm_fd = -1;
    }
}

/**
 * @brief 将API原始值（弧度）转换为显示值
 *        旋转电机：rad → deg
 *        线性电机：rad → mm（通过丝杠导程换算）
 *
 * @param axis_id 轴ID
 * @param raw_rad API返回的原始弧度值
 * @return 转换后的显示值（度或毫米）
 */
static double raw_to_display(int axis_id, double raw_rad)
{
    // 根据电机类型选择转换方式
    for (int i = 0; i < NUM_AXES; i++)
    {
        if (g_axis_ids[i] == axis_id)
        {
            if (g_axis_types[i] == MOTOR_TYPE_LINEAR)
            {
                // 线性电机：rad → mm
                return raw_rad * RAD_TO_LINEAR_MM;
            }
            break;
        }
    }
    // 旋转电机：rad → deg
    return raw_rad * RAD_TO_DEG;
}

/**
 * @brief 获取单位字符串
 *        旋转电机返回"deg"，线性电机返回"mm"
 */
static const char* unit_str(int axis_id)
{
    for (int i = 0; i < NUM_AXES; i++)
    {
        if (g_axis_ids[i] == axis_id)
        {
            if (g_axis_types[i] == MOTOR_TYPE_LINEAR)
                return "mm";
            break;
        }
    }
    return "deg";
}

/**
 * @brief 将显示速度值转换为SetAxisVelocity需要的API值（rad/s）
 *        旋转电机：deg/s → rad/s
 *        线性电机：mm/s → rad/s（通过丝杠导程换算）
 */
static double display_speed_to_api(int axis_id, double display_speed)
{
    for (int i = 0; i < NUM_AXES; i++)
    {
        if (g_axis_ids[i] == axis_id)
        {
            if (g_axis_types[i] == MOTOR_TYPE_LINEAR)
            {
                // 线性电机：mm/s → rad/s
                return display_speed / RAD_TO_LINEAR_MM;
            }
            break;
        }
    }
    // 旋转电机：deg/s → rad/s
    return display_speed * DEG_TO_RAD;
}

/**
 * @brief 对绝对值编码器的原始角度做展开（unwrap），得到连续的多圈总角度
 *
 * 绝对值编码器输出范围有限（如 ±π），当电机连续旋转时，
 * 编码器值会回绕。此函数通过检测相邻两次读数的跳变，
 * 持续累加得到连续的总旋转角度。
 *
 * 为防止电机静止时位置噪声导致误触发回绕检测，使用接近 2π 的阈值
 * （UNWRAP_THRESHOLD = π * 0.95），只有差值接近 2π 时才认为是真正的回绕。
 *
 * @param axis_idx 轴索引（0~NUM_AXES-1）
 * @param raw_rad 当前读取的原始绝对值角度（弧度）
 * @return 展开后的连续总角度（弧度）
 */
static double unwrap_angle(int axis_idx, double raw_rad)
{
    // 回绕检测阈值：接近 2π 但留有余量，防止噪声误触发
    // 电机在10ms间隔内最多旋转不可能超过 π rad（半圈），
    // 因此只有真正的编码器回绕才会产生接近 2π 的跳变
    const double kUnwrapThreshold = M_PI * 0.95;  // ≈ 2.98 rad

    if (g_linear_first_read[axis_idx])
    {
        // 第一次读取：记录原始值，总角度从当前值开始
        g_linear_prev_raw_rad[axis_idx] = raw_rad;
        g_linear_total_angle_rad[axis_idx] = raw_rad;
        g_linear_first_read[axis_idx] = false;
        return raw_rad;
    }

    // 计算与上一次的差值
    double diff = raw_rad - g_linear_prev_raw_rad[axis_idx];

    // 如果差值超过阈值，说明发生了编码器回绕
    // 正常运动时相邻两次采样（10ms间隔）的差值远小于此值，
    // 只有编码器从 +π 跳变到 -π（或反之）时才会产生接近 2π 的跳变
    if (diff > kUnwrapThreshold)
    {
        // 正向回绕：角度从 +π 附近跳变到 -π 附近
        // 例如：前一次=3.0 rad，当前=-3.0 rad，diff=-6.0 rad
        // 实际增量应为 diff + 2π
        diff -= 2.0 * M_PI;
    }
    else if (diff < -kUnwrapThreshold)
    {
        // 负向回绕：角度从 -π 附近跳变到 +π 附近
        // 例如：前一次=-3.0 rad，当前=3.0 rad，diff=6.0 rad
        // 实际增量应为 diff - 2π
        diff += 2.0 * M_PI;
    }

    // 更新上一次原始值
    g_linear_prev_raw_rad[axis_idx] = raw_rad;

    // 将增量累加到总角度
    g_linear_total_angle_rad[axis_idx] += diff;

    return g_linear_total_angle_rad[axis_idx];
}

/**
 * @brief 更新共享内存中的电机数据（在控制循环中每10ms调用一次）
 *
 * 位置和速度通过 GetAxisPosition/GetAxisVelocity 获取，
 * 力矩通过 GetAxisTorque 直接读取驱动器反馈（无需差分计算）。
 */
static void update_motor_data(const char* robot_name)
{
    if (!g_shm_data || !robot_name) return;

    int64_t now_us = get_timestamp_us();
    double dt_s = (g_prev_timestamp_us > 0)
                  ? (now_us - g_prev_timestamp_us) / 1000000.0
                  : 0.01; // 默认10ms

    // 低通滤波系数（0~1，越小越平滑，响应越慢）
    // 10ms 控制周期下，alpha=0.15 相当于约 15Hz 截止频率
    const double kFilterAlpha = 0.15;

    // 读取所有电机的位置、速度和力矩
    for (int i = 0; i < NUM_AXES; i++)
    {
        // GetAxisPosition() 和 GetAxisVelocity() 始终返回弧度(rad)和弧度/秒(rad/s)
        double pos_raw_rad = GetAxisPosition(robot_name, g_axis_ids[i]);
        double vel_raw_rad_per_s = GetAxisVelocity(robot_name, g_axis_ids[i]);

        // GetAxisTorque() 底层 get_axis_torque() 返回 short，单位为"额定电流千分比"（-1000~1000），
        // 需乘以 额定值/1000 得到真实物理值（Nm）。
        // 再减去零点偏移量进行力传感器零点校准。
        double torque_raw = GetAxisTorque(robot_name, g_axis_ids[i])
                            * (g_axis_rated_torque[i] / 1000.0)
                            - g_torque_zero_offset[i];

        double pos_display;
        double vel_display;

        if (g_axis_types[i] == MOTOR_TYPE_LINEAR)
        {
            // 线性电机：不需要角度展开（线性电机行程有限，不会多圈旋转）
            // rad → mm
            pos_display = pos_raw_rad * RAD_TO_LINEAR_MM;
            vel_display = vel_raw_rad_per_s * RAD_TO_LINEAR_MM;
        }
        else
        {
            // 旋转电机：需要做角度展开（unwrap），
            // 因为绝对值编码器输出范围有限（±π），电机连续多圈旋转时原始值会回绕。
            // 通过 unwrap 得到连续的总角度。
            double total_angle_rad = unwrap_angle(i, pos_raw_rad);
            // 弧度(rad) → 度(deg)
            pos_display = total_angle_rad * RAD_TO_DEG;
            vel_display = vel_raw_rad_per_s * RAD_TO_DEG;
        }

        // 一阶低通滤波平滑速度（驱动器读数仍有微小噪声）
        double vel_filtered = g_prev_filtered_velocity_display_per_s[i]
                              + kFilterAlpha * (vel_display - g_prev_filtered_velocity_display_per_s[i]);

        // 一阶低通滤波平滑力矩（驱动器反馈的原始力矩噪声较大）
        double torque_filtered = g_prev_filtered_torque[i]
                                 + kFilterAlpha * (torque_raw - g_prev_filtered_torque[i]);

        // 写入共享内存
        g_shm_data->axis_type[i] = g_axis_types[i];
        g_shm_data->position_deg[i] = pos_display;
        g_shm_data->velocity_deg_per_s[i] = vel_filtered;
        g_shm_data->torque_nm[i] = torque_filtered;  // 低通滤波后的力矩

        // 更新上一次值
        g_prev_position_display[i] = pos_display;
        g_prev_velocity_display_per_s[i] = vel_filtered;
        g_prev_filtered_velocity_display_per_s[i] = vel_filtered;
        g_prev_filtered_torque[i] = torque_filtered;
    }

    // 更新时间戳和序列号
    g_shm_data->timestamp_us = now_us;
    g_shm_data->sequence++;

    g_prev_timestamp_us = now_us;
}

static int enable_axis(const char* robot_name, int axis_id)
{
    printf("  使能轴 %d (速度模式)...\n", axis_id);

    // 先检查是否有错误，如果有则执行故障复位
    int error_status = axis_error_status(robot_name, axis_id);
    if (error_status)
    {
        printf("  轴 %d 有错误，执行故障复位...\n", axis_id);
        set_axis_control(robot_name, 0x0080, axis_id);
        usleep(100000);
        set_axis_control(robot_name, 0x0006, axis_id);
        usleep(100000);
        printf("  轴 %d 故障复位完成\n", axis_id);
        sleep(1);
    }

    // 使用速度模式 (mode 9)
    set_axis_mode(robot_name, 9, axis_id);
    sleep(1);
    axis_power_on(robot_name, axis_id);
    sleep(1);
    set_axis_control(robot_name, 0x000F, axis_id);
    sleep(1);

    int power_status = axis_power_status(robot_name, axis_id);
    error_status = axis_error_status(robot_name, axis_id);
    unsigned short axis_status = get_axis_status(robot_name, axis_id);
    printf("  轴 %d 状态: 使能=%d, 错误=%d, 状态字=0x%04X\n", axis_id, power_status, error_status, axis_status);

    // 更新共享内存中的状态
    if (g_shm_data)
    {
        g_shm_data->power_status[axis_id - 1] = power_status;
        g_shm_data->error_status[axis_id - 1] = error_status;
    }

    return power_status;
}

/**
 * @brief 慢速运动到目标位置（单位自适应：旋转电机用deg，线性电机用mm）
 * @param robot_name 机器人名称
 * @param axis_id 轴ID
 * @param target_position 目标位置（旋转电机：度，线性电机：毫米）
 * @param speed 速度（旋转电机：度/秒，线性电机：毫米/秒）
 *
 * 注意：位置读取统一通过 update_motor_data() 后的共享内存数据，
 * 避免 raw_to_display() 和 update_motor_data() 内部重复调用 unwrap_angle()
 * 导致全局累计角度状态不一致。
 */
static void move_to_position_slow(const char* robot_name, int axis_id, double target_position, double speed)
{
    const char* unit = unit_str(axis_id);

    // 先更新一次数据，确保共享内存中有最新位置
    update_motor_data(robot_name);

    // 从共享内存读取当前位置（已包含 unwrap 和单位换算）
    int axis_idx = -1;
    for (int i = 0; i < NUM_AXES; i++)
    {
        if (g_axis_ids[i] == axis_id)
        {
            axis_idx = i;
            break;
        }
    }
    double pos_current = (axis_idx >= 0) ? g_shm_data->position_deg[axis_idx]
                                         : raw_to_display(axis_id, GetAxisPosition(robot_name, axis_id));
    double pos_error = target_position - pos_current;
    double direction = (pos_error > 0) ? 1.0 : -1.0;

    printf("  轴 %d 慢速运动到目标，速度=%.4f %s/s\n", axis_id, speed, unit);

    // 持续发送速度指令，直到到达目标位置附近
    int timeout_count = 0;
    int max_timeout = 5000; // 约50秒超时
    do {
        // 更新共享内存数据（每10ms），内部会做 unwrap 和单位换算
        update_motor_data(robot_name);

        // 从共享内存读取最新位置
        pos_current = (axis_idx >= 0) ? g_shm_data->position_deg[axis_idx]
                                      : raw_to_display(axis_id, GetAxisPosition(robot_name, axis_id));
        pos_error = target_position - pos_current;

        // 如果已经到达或越过目标，停止（阈值0.1显示单位）
        if (fabs(pos_error) < 0.1 || (pos_error * direction) < 0)
        {
            break;
        }

        // 发送速度指令（SetAxisVelocity 接收 rad/s，需转换）
        double api_speed = display_speed_to_api(axis_id, direction * speed);
        SetAxisVelocity(robot_name, api_speed, axis_id);
        usleep(10000); // 10ms
        timeout_count++;

        if (timeout_count % 100 == 0)
        {
            printf("  轴 %d 当前位置: %.2f %s, 目标: %.2f %s, 偏差: %.2f %s\n",
                   axis_id, pos_current, unit,
                   target_position, unit,
                   pos_error, unit);
        }
    } while (robot_ok() && timeout_count < max_timeout);

    // 停止
    SetAxisVelocity(robot_name, 0.0, axis_id);
    // 停止后再次更新数据
    update_motor_data(robot_name);
    printf("  轴 %d 到达目标位置: %.2f %s\n", axis_id,
           raw_to_display(axis_id, GetAxisPosition(robot_name, axis_id)), unit);
}

/**
 * @brief 处理来自 Qt 界面的共享内存控制请求
 *
 * 在控制主循环中调用，检查 g_shm_data->cmd_type，
 * 如果有新的控制命令则执行。
 *
 * @param robot_name 机器人名称
 */
static void process_control_commands(const char* robot_name)
{
    if (!g_shm_data || !robot_name) return;

    // ========== 多轴运动处理（优先于单轴命令） ==========
    static uint64_t last_multi_cmd_sequence = 0;

    if (g_shm_data->multi_cmd_status == 1)
    {
        // 多轴运动正在执行
        bool all_arrived = true;
        bool any_active = false;

        for (int i = 0; i < NUM_AXES; i++)
        {
            if (!g_shm_data->multi_active[i]) continue;
            any_active = true;

            int axis_id = g_axis_ids[i];
            double target = g_shm_data->multi_target_pos[i];
            double speed = g_shm_data->multi_speed[i];
            double pos_current = g_shm_data->position_deg[i];
            double pos_error = target - pos_current;
            double direction = (pos_error > 0) ? 1.0 : -1.0;

            // 如果已到达或越过目标
            if (fabs(pos_error) < 0.1 || (pos_error * direction) < 0)
            {
                // 停止该轴
                SetAxisVelocity(robot_name, 0.0, axis_id);
                g_shm_data->multi_axis_status[i] = 2; // 已到达
                printf("[多轴] 轴%d 到达目标: %.2f (当前: %.2f)\n",
                       axis_id, target, pos_current);
            }
            else
            {
                // 检查软限位（只阻止往危险方向运动）
                if (check_soft_limit(robot_name, i, direction))
                {
                    g_shm_data->multi_axis_status[i] = 3; // 限位触发
                    printf("[多轴] 轴%d 因限位停止\n", axis_id);
                    continue;
                }

                // 继续运动
                all_arrived = false;
                g_shm_data->multi_axis_status[i] = 1; // 运动中
                // 旋转电机速度固定为1 deg/s，直线电机使用设定速度或默认2.0 mm/s
                if (g_axis_types[i] == MOTOR_TYPE_LINEAR)
                {
                    if (speed <= 0) speed = 2.0;
                }
                else
                {
                    speed = 1.0; // 旋转电机统一速度1 deg/s
                }
                double api_speed = display_speed_to_api(axis_id, direction * speed);
                SetAxisVelocity(robot_name, api_speed, axis_id);
            }
        }

        // 更新监控数据
        update_motor_data(robot_name);

        // 如果所有 active 的轴都已到达，标记完成
        if (any_active && all_arrived)
        {
            g_shm_data->multi_cmd_status = 2;
            snprintf(g_shm_data->multi_cmd_result, sizeof(g_shm_data->multi_cmd_result),
                     "多轴运动完成");
            printf("[多轴] 所有轴已到达目标位置\n");
        }

        // 多轴运动进行中，不处理单轴命令
        return;
    }

    // 检查是否有新的多轴命令
    if (g_shm_data->multi_cmd_sequence != last_multi_cmd_sequence
        && g_shm_data->multi_cmd_status == 1)
    {
        last_multi_cmd_sequence = g_shm_data->multi_cmd_sequence;
        printf("[多轴] 开始多轴运动\n");

        // 初始化每个 active 轴的状态
        for (int i = 0; i < NUM_AXES; i++)
        {
            if (g_shm_data->multi_active[i])
            {
                int axis_id = g_axis_ids[i];
                double target = g_shm_data->multi_target_pos[i];
                double speed = g_shm_data->multi_speed[i];
                double pos_current = g_shm_data->position_deg[i];

                // 旋转电机速度固定为1 deg/s，直线电机使用设定速度或默认2.0 mm/s
                if (g_axis_types[i] == MOTOR_TYPE_LINEAR)
                {
                    if (speed <= 0) speed = 2.0;
                }
                else
                {
                    speed = 1.0; // 旋转电机统一速度1 deg/s
                }

                double direction = (target > pos_current) ? 1.0 : -1.0;

                // 检查软限位（只阻止往危险方向运动）
                if (check_soft_limit(robot_name, i, direction))
                {
                    g_shm_data->multi_axis_status[i] = 3; // 限位触发
                    printf("[多轴] 轴%d 因限位跳过\n", axis_id);
                    continue;
                }

                double api_speed = display_speed_to_api(axis_id, direction * speed);

                g_shm_data->multi_axis_status[i] = 1; // 运动中
                SetAxisVelocity(robot_name, api_speed, axis_id);

                printf("[多轴] 轴%d: %.2f -> %.2f (速度: %.1f)\n",
                       axis_id, pos_current, target, speed);
            }
        }
        update_motor_data(robot_name);
        return;
    }

    // ========== 单轴命令处理 ==========

    // 检查是否有新的命令（通过 cmd_sequence 判断）
    static uint64_t last_cmd_sequence = 0;

    int32_t cmd = g_shm_data->cmd_type;
    if (cmd == CMD_NONE) return;

    // 如果序列号没变，说明是同一个命令（已处理或正在处理）
    if (g_shm_data->cmd_sequence == last_cmd_sequence)
    {
        // 如果命令正在执行，继续执行
        if (g_shm_data->cmd_status == 1)
        {
            // 速度模式：持续发送速度指令，直到收到停止命令
            if (cmd == CMD_VELOCITY_MODE)
            {
                int axis_idx = g_shm_data->cmd_axis_idx;
                if (axis_idx >= 0 && axis_idx < NUM_AXES)
                {
                    int axis_id = g_axis_ids[axis_idx];
                    // 检查软限位（根据速度方向判断）
                    double vel_dir = (g_shm_data->cmd_value >= 0) ? 1.0 : -1.0;
                    if (check_soft_limit(robot_name, axis_idx, vel_dir))
                    {
                        g_shm_data->cmd_status = 2;
                        g_shm_data->cmd_type = CMD_NONE;
                        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                                 "轴%d 因限位停止", axis_id);
                        printf("[Qt控制] 轴%d 速度模式因限位停止\n", axis_id);
                    }
                    else
                    {
                        double vel = g_shm_data->cmd_value;
                        double api_speed = display_speed_to_api(axis_id, vel);
                        SetAxisVelocity(robot_name, api_speed, axis_id);
                    }
                    update_motor_data(robot_name);
                }
            }
            // 位置模式：检查是否到达目标位置
            else if (cmd == CMD_MOVE_TO || cmd == CMD_MOVE_ABS)
            {
                int axis_idx = g_shm_data->cmd_axis_idx;
                if (axis_idx >= 0 && axis_idx < NUM_AXES)
                {
                    int axis_id = g_axis_ids[axis_idx];
                    double target = g_shm_data->cmd_target_position;
                    double pos_current = g_shm_data->position_deg[axis_idx];
                    double pos_error = target - pos_current;
                    double direction = (pos_error > 0) ? 1.0 : -1.0;

                    // 如果已经到达或越过目标，停止
                    if (fabs(pos_error) < 0.1 || (pos_error * direction) < 0)
                    {
                        SetAxisVelocity(robot_name, 0.0, axis_id);
                        update_motor_data(robot_name);
                        g_shm_data->cmd_status = 2;
                        g_shm_data->cmd_type = CMD_NONE;
                        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                                 "轴%d 到达目标 %.2f %s", axis_id, target, unit_str(axis_id));
                        printf("[Qt控制] 轴%d 到达目标位置: %.2f %s\n",
                               axis_id, pos_current, unit_str(axis_id));
                    }
                    else
                    {
                        // 检查软限位（根据运动方向判断）
                        if (check_soft_limit(robot_name, axis_idx, direction))
                        {
                            SetAxisVelocity(robot_name, 0.0, axis_id);
                            update_motor_data(robot_name);
                            g_shm_data->cmd_status = 2;
                            g_shm_data->cmd_type = CMD_NONE;
                            snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                                     "轴%d 因限位停止", axis_id);
                            printf("[Qt控制] 轴%d 位置模式因限位停止\n", axis_id);
                        }
                        else
                        {
                            // 继续发送速度指令
                            double speed = g_shm_data->cmd_speed;
                            double api_speed = display_speed_to_api(axis_id, direction * speed);
                            SetAxisVelocity(robot_name, api_speed, axis_id);
                        }
                        update_motor_data(robot_name);
                    }
                }
            }
        }
        return;
    }

    // 新命令到达
    last_cmd_sequence = g_shm_data->cmd_sequence;
    int axis_idx = g_shm_data->cmd_axis_idx;

    if (axis_idx < 0 || axis_idx >= NUM_AXES)
    {
        g_shm_data->cmd_status = -1;
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "无效轴索引: %d", axis_idx);
        g_shm_data->cmd_type = CMD_NONE;
        return;
    }

    int axis_id = g_axis_ids[axis_idx];

    switch (cmd)
    {
    case CMD_MOVE_TO:
    {
        // 相对运动（位置模式）
        double pos_current = g_shm_data->position_deg[axis_idx];
        double target = pos_current + g_shm_data->cmd_value;
        double speed = g_shm_data->cmd_speed;

        if (speed <= 0)
        {
            speed = (g_axis_types[axis_idx] == MOTOR_TYPE_LINEAR) ? 2.0 : 5.0;
        }

        double direction = (target > pos_current) ? 1.0 : -1.0;
        // 检查软限位：只阻止往危险方向运动
        if (check_soft_limit(robot_name, axis_idx, direction))
        {
            g_shm_data->cmd_status = -1;
            snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                     "轴%d 处于限位边界，无法运动", axis_id);
            g_shm_data->cmd_type = CMD_NONE;
            printf("[Qt控制] 轴%d 相对运动被限位阻止\n", axis_id);
            break;
        }

        // 保存目标位置到 cmd_target_position，后续循环中持续检查
        g_shm_data->cmd_target_position = target;
        g_shm_data->cmd_status = 1; // 正在执行
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "轴%d 运动到 %.2f %s", axis_id, target, unit_str(axis_id));

        printf("[Qt控制] 轴%d 相对运动: %.2f -> %.2f (速度: %.2f)\n",
               axis_id, pos_current, target, speed);

        // 发送第一次速度指令
        double api_speed = display_speed_to_api(axis_id, direction * speed);
        SetAxisVelocity(robot_name, api_speed, axis_id);
        break;
    }

    case CMD_MOVE_ABS:
    {
        // 绝对运动（位置模式）
        double target = g_shm_data->cmd_value;
        double speed = g_shm_data->cmd_speed;

        if (speed <= 0)
        {
            speed = (g_axis_types[axis_idx] == MOTOR_TYPE_LINEAR) ? 2.0 : 5.0;
        }

        double pos_current = g_shm_data->position_deg[axis_idx];

        double direction = (target > pos_current) ? 1.0 : -1.0;
        // 检查软限位：只阻止往危险方向运动
        if (check_soft_limit(robot_name, axis_idx, direction))
        {
            g_shm_data->cmd_status = -1;
            snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                     "轴%d 处于限位边界，无法运动", axis_id);
            g_shm_data->cmd_type = CMD_NONE;
            printf("[Qt控制] 轴%d 绝对运动被限位阻止\n", axis_id);
            break;
        }

        // 保存目标位置到 cmd_target_position，后续循环中持续检查
        g_shm_data->cmd_target_position = target;
        g_shm_data->cmd_status = 1;
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "轴%d 绝对运动到 %.2f %s", axis_id, target, unit_str(axis_id));

        printf("[Qt控制] 轴%d 绝对运动到: %.2f (速度: %.2f)\n",
               axis_id, target, speed);

        // 发送第一次速度指令
        double api_speed = display_speed_to_api(axis_id, direction * speed);
        SetAxisVelocity(robot_name, api_speed, axis_id);
        break;
    }

    case CMD_VELOCITY_MODE:
    {
        // 速度模式：以指定速度持续运动（正=正向，负=反向）
        double vel = g_shm_data->cmd_value;
        double speed = fabs(vel);
        if (speed <= 0)
        {
            speed = (g_axis_types[axis_idx] == MOTOR_TYPE_LINEAR) ? 2.0 : 5.0;
            vel = (vel >= 0) ? speed : -speed;
        }

        double direction = (vel >= 0) ? 1.0 : -1.0;
        // 检查软限位：只阻止往危险方向运动
        if (check_soft_limit(robot_name, axis_idx, direction))
        {
            g_shm_data->cmd_status = -1;
            snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                     "轴%d 处于限位边界，无法运动", axis_id);
            g_shm_data->cmd_type = CMD_NONE;
            printf("[Qt控制] 轴%d 速度模式被限位阻止\n", axis_id);
            break;
        }

        double api_speed = display_speed_to_api(axis_id, direction * speed);

        g_shm_data->cmd_status = 1; // 正在执行（持续运动）
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "轴%d 速度模式 %.1f %s/s", axis_id, direction * speed, unit_str(axis_id));

        printf("[Qt控制] 轴%d 速度模式: %.1f %s/s\n",
               axis_id, direction * speed, unit_str(axis_id));

        SetAxisVelocity(robot_name, api_speed, axis_id);
        break;
    }

    case CMD_VELOCITY_STOP:
    {
        // 停止速度模式运动
        printf("[Qt控制] 停止轴%d 速度模式\n", axis_id);
        SetAxisVelocity(robot_name, 0.0, axis_id);
        update_motor_data(robot_name);
        g_shm_data->cmd_status = 2;
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "轴%d 速度模式已停止", axis_id);
        g_shm_data->cmd_type = CMD_NONE;
        break;
    }

    case CMD_STOP:
    {
        printf("[Qt控制] 停止轴%d\n", axis_id);
        SetAxisVelocity(robot_name, 0.0, axis_id);
        update_motor_data(robot_name);
        g_shm_data->cmd_status = 2;
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "轴%d 已停止", axis_id);
        g_shm_data->cmd_type = CMD_NONE;
        break;
    }

    case CMD_ENABLE:
    {
        printf("[Qt控制] 使能轴%d\n", axis_id);
        int ret = enable_axis(robot_name, axis_id);
        g_shm_data->cmd_status = (ret > 0) ? 2 : -1;
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "轴%d 使能 %s", axis_id, (ret > 0) ? "成功" : "失败");
        g_shm_data->cmd_type = CMD_NONE;
        break;
    }

    case CMD_DISABLE:
    {
        printf("[Qt控制] 失能轴%d\n", axis_id);
        axis_power_off(robot_name, axis_id);
        g_shm_data->cmd_status = 2;
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "轴%d 已失能", axis_id);
        g_shm_data->cmd_type = CMD_NONE;
        break;
    }

    default:
        g_shm_data->cmd_status = -1;
        snprintf(g_shm_data->cmd_result, sizeof(g_shm_data->cmd_result),
                 "未知命令: %d", cmd);
        g_shm_data->cmd_type = CMD_NONE;
        break;
    }
}

/**
 * @brief 处理动作序列队列（循环执行多组动作）
 *
 * 当 seq_status == 1 时，按顺序执行队列中的每组动作。
 * 当前组所有 active 轴到达目标后，自动推进到下一组。
 * 如果 seq_looping == 1，最后一组执行完后回到第 1 组继续循环。
 *
 * @param robot_name 机器人名称
 */
// 记录上一次处理的组号，用于检测组切换
static int g_seq_last_group = -1;
// 当前组每个轴的比例调整速度（仅在推进到新组时计算一次）
static double g_seq_adjusted_speed_deg_per_s[NUM_AXES] = {0.0};

static void process_sequence_queue(const char* robot_name)
{
    if (!g_shm_data || !robot_name) return;
    if (g_shm_data->seq_status != 1) return;  // 序列未运行

    int group_count = g_shm_data->seq_group_count;
    if (group_count <= 0)
    {
        g_shm_data->seq_status = 0;
        return;
    }

    int current_group = g_shm_data->seq_current_group;
    if (current_group < 0) current_group = 0;
    if (current_group >= group_count) current_group = 0;

    // ========== 组切换检测：推进到新组时，重新计算时间同步速度 ==========
    if (current_group != g_seq_last_group)
    {
        g_seq_last_group = current_group;

        // 第1步：收集所有 active 轴，计算每个轴的移动距离
        double max_distance = 0.0;
        bool has_active = false;

        for (int i = 0; i < NUM_AXES; i++)
        {
            if (!g_shm_data->seq_active[current_group][i]) continue;
            has_active = true;

            double target = g_shm_data->seq_target[current_group][i];
            double pos_current = g_shm_data->position_deg[i];
            double distance = fabs(target - pos_current);
            if (distance > max_distance) max_distance = distance;
        }

        // 第2步：按比例调整每个轴的速度
        //        基准速度使用 Qt 设定的 seq_speed[current_group][i]
        //        所有轴执行时间 = max_distance / base_speed_max
        //        轴i调整后速度 = base_speed_i × (distance_i / max_distance)
        for (int i = 0; i < NUM_AXES; i++)
        {
            if (!g_shm_data->seq_active[current_group][i]) continue;

            double base_speed = g_shm_data->seq_speed[current_group][i];
            if (base_speed <= 0.0)
            {
                // 默认基准速度：旋转电机 1 deg/s，直线电机 2 mm/s
                base_speed = (g_axis_types[i] == MOTOR_TYPE_LINEAR) ? 2.0 : 1.0;
            }

            double target = g_shm_data->seq_target[current_group][i];
            double pos_current = g_shm_data->position_deg[i];
            double distance = fabs(target - pos_current);

            // 比例调整：运行距离越短的轴，速度越慢
            if (max_distance > 0.01)
            {
                g_seq_adjusted_speed_deg_per_s[i] = base_speed * (distance / max_distance);
                // 最低速度不低于 0.1 单位/秒，避免太慢无法到达
                if (g_seq_adjusted_speed_deg_per_s[i] < 0.1)
                    g_seq_adjusted_speed_deg_per_s[i] = 0.1;
            }
            else
            {
                // 已经在目标位置附近，速度设为最小
                g_seq_adjusted_speed_deg_per_s[i] = 0.1;
            }

            printf("[序列] 组%d 轴%d: 距离=%.2f, 基准速度=%.2f, 调整后=%.2f (%s)\n",
                   current_group + 1, g_axis_ids[i], distance, base_speed,
                   g_seq_adjusted_speed_deg_per_s[i],
                   (g_axis_types[i] == MOTOR_TYPE_LINEAR) ? "mm/s" : "deg/s");
        }

        // 第3步：膝速度 = 对应髋速度 × 1.3
        // 映射：右膝(idx2) ← 右髋(idx3)，左膝(idx6) ← 左髋(idx4)
        if (g_shm_data->seq_active[current_group][2] && g_shm_data->seq_active[current_group][3])
        {
            g_seq_adjusted_speed_deg_per_s[2] = g_seq_adjusted_speed_deg_per_s[3] * 1.3;
            printf("[序列] 组%d 右膝(idx2): 速度同步为右髋(idx3)的1.3倍, 最终=%.2f deg/s\n",
                   current_group + 1, g_seq_adjusted_speed_deg_per_s[2]);
        }
        if (g_shm_data->seq_active[current_group][6] && g_shm_data->seq_active[current_group][4])
        {
            g_seq_adjusted_speed_deg_per_s[6] = g_seq_adjusted_speed_deg_per_s[4] * 1.3;
            printf("[序列] 组%d 左膝(idx6): 速度同步为左髋(idx4)的1.3倍, 最终=%.2f deg/s\n",
                   current_group + 1, g_seq_adjusted_speed_deg_per_s[6]);
        }

        if (has_active)
        {
            printf("[序列] 组%d 最远距离=%.2f, 已同步各轴速度（膝速度=髋速度×1.3）\n",
                   current_group + 1, max_distance);
        }
    }

    // ========== 处理当前组中每个轴（使用时间同步速度） ==========
    bool all_arrived = true;
    bool any_active = false;

    for (int i = 0; i < NUM_AXES; i++)
    {
        if (!g_shm_data->seq_active[current_group][i]) continue;
        any_active = true;

        int axis_id = g_axis_ids[i];
        double target = g_shm_data->seq_target[current_group][i];
        double speed = g_seq_adjusted_speed_deg_per_s[i]; // 使用同步后的速度
        double pos_current = g_shm_data->position_deg[i];
        double pos_error = target - pos_current;
        double direction = (pos_error > 0) ? 1.0 : -1.0;

        // 已到达或越过目标
        if (fabs(pos_error) < 0.1 || (pos_error * direction) < 0)
        {
            SetAxisVelocity(robot_name, 0.0, axis_id);
            g_shm_data->seq_axis_status[i] = 2; // 已到达
        }
        else
        {
            // 检查软限位
            if (check_soft_limit(robot_name, i, direction))
            {
                g_shm_data->seq_axis_status[i] = 3; // 限位触发
                printf("[序列] 组%d 轴%d 因限位停止\n", current_group + 1, axis_id);
                continue;
            }

            all_arrived = false;
            g_shm_data->seq_axis_status[i] = 1; // 运动中

            double api_speed = display_speed_to_api(axis_id, direction * speed);
            SetAxisVelocity(robot_name, api_speed, axis_id);
        }
    }

    // 更新监控数据
    update_motor_data(robot_name);

    // 当前组所有 active 轴都到达 → 推进到下一组
    if (any_active && all_arrived)
    {
        printf("[序列] 组%d/%d 完成\n", current_group + 1, group_count);

        int next_group = current_group + 1;
        if (next_group >= group_count)
        {
            if (g_shm_data->seq_looping)
            {
                // 循环：回到第1组
                next_group = 0;
                printf("[序列] 循环回到组1\n");
            }
            else
            {
                // 单次模式：完成
                g_shm_data->seq_status = 0;
                g_shm_data->seq_current_group = 0;
                snprintf(g_shm_data->seq_result, sizeof(g_shm_data->seq_result),
                         "序列完成（共 %d 组）", group_count);
                printf("[序列] 全部 %d 组完成\n", group_count);

                // 停止所有轴
                for (int i = 0; i < NUM_AXES; i++)
                {
                    if (g_shm_data->seq_active[current_group][i])
                        SetAxisVelocity(robot_name, 0.0, g_axis_ids[i]);
                }
                return;
            }
        }

        // 切换到下一组，重置轴状态
        g_shm_data->seq_current_group = next_group;
        memset(g_shm_data->seq_axis_status, 0, NUM_AXES * sizeof(int32_t));

        snprintf(g_shm_data->seq_result, sizeof(g_shm_data->seq_result),
                 "执行组%d/%d", next_group + 1, group_count);
        printf("[序列] 推进到组%d/%d\n", next_group + 1, group_count);
    }
}

int ServoDemo()
{
    const char* robot_name = get_name_robot_device(get_deviceName(0, NULL), 0);
    get_control_cycle(get_deviceName(0, NULL));

    // 初始化共享内存
    if (init_shared_memory() != 0)
    {
        printf("警告: 共享内存初始化失败，Qt监控界面将无法连接\n");
    }

    // 1. 使能所有电机 (速度模式)
    printf("1. 使能所有 %d 个电机 (axis_ID: 1~%d)...\n", NUM_AXES, NUM_AXES);
    for (int i = 0; i < NUM_AXES; i++)
    {
        enable_axis(robot_name, g_axis_ids[i]);
    }

    printf("\n=== 慢速位置控制 ===\n");
    printf("输入格式: 轴号 位置量\n");
    printf("例如: 1 90  表示轴1转动90度\n");
    printf("      2 5   表示轴2移动5毫米\n");
    printf("按回车回到原位，然后可继续输入\n\n");

    // 各轴慢速参数
    double slow_speed = 5.0;        // 旋转电机：5 deg/s
    double slow_speed_linear = 2.0; // 线性电机：2 mm/s

    // 将 stdin 设为非阻塞模式，使控制循环能持续运行
    int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK);

    // 终端输入缓冲区
    char input_buf[256];
    int input_buf_len = 0;

    while (robot_ok())
    {
        // 更新共享内存数据（保持曲线刷新）
        update_motor_data(robot_name);

        // 处理动作序列队列（循环执行多组动作）
        process_sequence_queue(robot_name);

        // 处理来自 Qt 界面的控制请求（每次循环都处理，不阻塞）
        process_control_commands(robot_name);

        // 如果有 Qt 控制命令正在执行（位置模式），跳过终端输入
        // 速度模式(CMD_VELOCITY_MODE)不阻塞终端输入
        if (g_shm_data && g_shm_data->cmd_status == 1
            && g_shm_data->cmd_type != CMD_VELOCITY_MODE)
        {
            usleep(10000);
            continue;
        }

        // 非阻塞读取终端输入
        // 每次循环读取一个字符，遇到换行符时解析整行
        char ch;
        int n = read(STDIN_FILENO, &ch, 1);
        if (n > 0)
        {
            if (ch == '\n')
            {
                // 解析输入行
                input_buf[input_buf_len] = '\0';
                if (input_buf_len > 0)
                {
                    int axis_id = 0;
                    double input_value = 0.0;
                    if (sscanf(input_buf, "%d %lf", &axis_id, &input_value) == 2)
                    {
                        // 检查轴号是否有效
                        bool valid = false;
                        int axis_idx = -1;
                        for (int i = 0; i < NUM_AXES; i++)
                        {
                            if (g_axis_ids[i] == axis_id)
                            {
                                valid = true;
                                axis_idx = i;
                                break;
                            }
                        }
                        if (valid)
                        {
                            const char* unit = unit_str(axis_id);
                            double pos_start = g_shm_data->position_deg[axis_idx];
                            printf("  轴 %d 当前位置: %.2f %s\n", axis_id, pos_start, unit);

                            double speed = (g_axis_types[axis_idx] == MOTOR_TYPE_LINEAR) ? slow_speed_linear : slow_speed;
                            double target_position = pos_start + input_value;
                            printf("  轴 %d 目标位置: %.2f %s (速度: %.1f %s/s)\n",
                                   axis_id, target_position, unit, speed, unit);

                            move_to_position_slow(robot_name, axis_id, target_position, speed);

                            printf("\n按回车键回到原位...\n");
                            fflush(stdout);

                            // 等待用户按回车回到原位（非阻塞方式）
                            // 这里用一个内部循环等待，同时持续处理 Qt 命令
                            bool wait_for_return = true;
                            int ret_buf_len = 0;
                            char ret_buf[256];
                            while (wait_for_return && robot_ok())
                            {
                                // 持续处理 Qt 命令
                                update_motor_data(robot_name);
                                process_control_commands(robot_name);

                                char rc;
                                int rn = read(STDIN_FILENO, &rc, 1);
                                if (rn > 0)
                                {
                                    if (rc == '\n')
                                    {
                                        ret_buf[ret_buf_len] = '\0';
                                        wait_for_return = false;
                                    }
                                    else if (ret_buf_len < 255)
                                    {
                                        ret_buf[ret_buf_len++] = rc;
                                    }
                                }
                                else
                                {
                                    usleep(10000);
                                }
                            }

                            // 慢速回到起始位置
                            printf("  轴 %d 回到原位...\n", axis_id);
                            move_to_position_slow(robot_name, axis_id, pos_start, speed);
                            printf("\n");
                        }
                        else
                        {
                            printf("无效轴号，请输入 1~%d\n", NUM_AXES);
                        }
                    }
                    else
                    {
                        printf("输入格式错误，请重新输入 (格式: 轴号 位置量)\n");
                    }
                }
                input_buf_len = 0;
            }
            else if (input_buf_len < 255)
            {
                input_buf[input_buf_len++] = ch;
            }
        }
        else
        {
            // 没有输入时，短暂休眠避免 busy loop
            usleep(10000);
        }
    }

    // 恢复 stdin 阻塞模式
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags);

    printf("\n演示结束\n");

    // 清理共享内存
    cleanup_shared_memory();

    return 0;
}

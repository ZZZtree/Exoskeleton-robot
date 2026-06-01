#include "HYYRobotInterface.h"
#include "MotorMonitorData.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
using namespace HYYRobotBase;

#define NUM_AXES 4
static const int g_axis_ids[NUM_AXES] = {1, 2, 3, 4};

// 电机类型：0=旋转电机（角度），1=线性电机（毫米）
// 轴1,2,3为旋转电机，轴4为线性电机（旋转电机+丝杠，导程5mm）
static const int g_axis_types[NUM_AXES] = {
    MOTOR_TYPE_ROTARY,  // 轴1 - 旋转
    MOTOR_TYPE_ROTARY,  // 轴2 - 旋转
    MOTOR_TYPE_ROTARY,  // 轴3 - 旋转
    MOTOR_TYPE_LINEAR   // 轴4 - 线性（丝杠导程5mm）
};

// 线性电机丝杠导程（mm/圈）：丝杠旋转一圈，滑块移动5mm
#define LINEAR_LEAD_MM 5.0

// 弧度与角度转换常量
#define RAD_TO_DEG (180.0 / M_PI)
#define DEG_TO_RAD (M_PI / 180.0)

// 线性电机换算常量
// GetAxisPosition() 返回弧度（旋转角度），需换算为直线位移：
//   位移(mm) = 角度(rad) * (180/π) / 360 * 导程(5mm)
//            = 角度(rad) * 导程 / (2π)
#define RAD_TO_LINEAR_MM (LINEAR_LEAD_MM / (2.0 * M_PI))

// 电机额定扭矩/额定力参数
// GetAxisTorque() 底层 get_axis_torque() 返回 short，单位为"额定电流千分比"（-1000~1000），
// 需乘以 额定值/1000 得到真实物理值（Nm 或 N）。
// 额定扭矩 = 9550 × 功率(kW) / 转速(rpm) × 减速比
// 轴1 (MJB25T): 500W, 24.5rpm(减速器输出端), 减速比120 → 输出端额定扭矩 ≈ 194.9 Nm
// 轴2/3 (MJB20T): 250W, 35rpm(减速器输出端), 减速比120 → 输出端额定扭矩 ≈ 68.2 Nm
// 轴4: 线性电机最大推力 6000 N
static const double g_axis_rated_torque[NUM_AXES] = {
    194.9,   // 轴1 - 减速器输出端额定扭矩 194.9 Nm
    68.2,    // 轴2 - 减速器输出端额定扭矩 68.2 Nm
    68.2,    // 轴3 - 减速器输出端额定扭矩 68.2 Nm
    6000.0   // 轴4 - 线性电机最大推力 6000 N
};

// 力矩零点偏移量（用于力传感器零点校准）
// 各轴静态时力矩读数不为0，减去偏移量使静态显示为0。
// 偏移量单位为 Nm 或 N（在 GetAxisTorque() * 额定值/1000 之后减去）。
// 轴2/3：静态力矩约 -260 Nm → 偏移 -260.0 Nm
// 轴4：静态力矩约 -1800 N，但轴4力矩已禁用显示
static const double g_torque_zero_offset[NUM_AXES] = {
    0.0,      // 轴1 - 无偏移
    0.0,   // 轴2 - 静态偏移 -260 Nm
    0.0,   // 轴3 - 静态偏移 -260 Nm
    -1800.0   // 轴4 - 零点偏移 -1800 N（已禁用显示）
};

// 共享内存相关
static int g_shm_fd = -1;
static MotorMonitorData* g_shm_data = NULL;

// 上一次位置（用于计算速度）
// 旋转电机：度(deg)，线性电机：毫米(mm)
static double g_prev_position_display[NUM_AXES] = {0.0, 0.0, 0.0, 0.0};
static double g_prev_velocity_display_per_s[NUM_AXES] = {0.0, 0.0, 0.0, 0.0};
static int64_t g_prev_timestamp_us = 0;

// 低通滤波后的速度（用于平滑位置噪声）
static double g_prev_filtered_velocity_display_per_s[NUM_AXES] = {0.0, 0.0, 0.0, 0.0};

// 低通滤波后的力矩（驱动器反馈的原始力矩噪声较大）
static double g_prev_filtered_torque[NUM_AXES] = {0.0, 0.0, 0.0, 0.0};

// ============================================================
// 绝对值编码器 → 连续多圈角度展开
// ============================================================
// 所有电机（轴1~4）都使用绝对值编码器，每次上电能读到绝对角度。
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
static double g_linear_total_angle_rad[NUM_AXES] = {0.0, 0.0, 0.0, 0.0};
static double g_linear_prev_raw_rad[NUM_AXES] = {0.0, 0.0, 0.0, 0.0};
static bool g_linear_first_read[NUM_AXES] = {true, true, true, true};

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
 *        线性电机：rad → mm（通过导程换算，含角度展开）
 *
 *        注意：线性电机的原始角度需要做 unwrap 才能得到正确的连续位置。
 *        这里调用 unwrap_angle() 维护全局累计角度，确保多次调用结果连续。
 *
 * @param axis_id 轴ID
 * @param raw_rad API返回的原始弧度值
 * @return 转换后的显示值
 */
static double raw_to_display(int axis_id, double raw_rad)
{
    for (int i = 0; i < NUM_AXES; i++)
    {
        if (g_axis_ids[i] == axis_id)
        {
            // 所有电机都需要做角度展开（unwrap），因为绝对值编码器输出范围有限（±π），
            // 电机连续多圈旋转时原始值会回绕。通过 unwrap 得到连续的总角度。
            double total_angle_rad = unwrap_angle(i, raw_rad);

            if (g_axis_types[i] == MOTOR_TYPE_ROTARY)
                return total_angle_rad * RAD_TO_DEG;  // rad → deg
            else
                // 线性电机：通过导程换算为直线位移
                return total_angle_rad * RAD_TO_LINEAR_MM;  // rad → mm
        }
    }
    return raw_rad;
}

/**
 * @brief 根据电机类型获取单位字符串
 */
static const char* unit_str(int axis_id)
{
    for (int i = 0; i < NUM_AXES; i++)
    {
        if (g_axis_ids[i] == axis_id)
        {
            return (g_axis_types[i] == MOTOR_TYPE_LINEAR) ? "mm" : "deg";
        }
    }
    return "?";
}

/**
 * @brief 将显示速度值转换为SetAxisVelocity需要的API值
 *        旋转电机：deg/s → rad/s
 *        线性电机：mm/s → rad/s（通过导程反算）
 */
static double display_speed_to_api(int axis_id, double display_speed)
{
    for (int i = 0; i < NUM_AXES; i++)
    {
        if (g_axis_ids[i] == axis_id)
        {
            if (g_axis_types[i] == MOTOR_TYPE_ROTARY)
                return display_speed * DEG_TO_RAD;  // deg/s → rad/s
            else
                // mm/s → rad/s：速度(mm/s) / 导程(mm/圈) * 2π(rad/圈)
                // 简化：rad/s = mm/s / RAD_TO_LINEAR_MM
                return display_speed / RAD_TO_LINEAR_MM;  // mm/s → rad/s
        }
    }
    return display_speed;
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

    // 读取四个电机的位置、速度和力矩
    for (int i = 0; i < NUM_AXES; i++)
    {
        // GetAxisPosition() 和 GetAxisVelocity() 始终返回弧度(rad)和弧度/秒(rad/s)
        double pos_raw_rad = GetAxisPosition(robot_name, g_axis_ids[i]);
        double vel_raw_rad_per_s = GetAxisVelocity(robot_name, g_axis_ids[i]);

        // GetAxisTorque() 底层 get_axis_torque() 返回 short，单位为"额定电流千分比"（-1000~1000），
        // 需乘以 额定值/1000 得到真实物理值（Nm 或 N）。
        // 再减去零点偏移量进行力传感器零点校准。
        double torque_raw = GetAxisTorque(robot_name, g_axis_ids[i])
                            * (g_axis_rated_torque[i] / 1000.0)
                            - g_torque_zero_offset[i];

        double pos_display, vel_display;

        // 所有电机都需要做角度展开（unwrap），因为绝对值编码器输出范围有限（±π），
        // 电机连续多圈旋转时原始值会回绕。通过 unwrap 得到连续的总角度。
        double total_angle_rad = unwrap_angle(i, pos_raw_rad);

        if (g_axis_types[i] == MOTOR_TYPE_ROTARY)
        {
            // 旋转电机：弧度(rad) → 度(deg)
            pos_display = total_angle_rad * RAD_TO_DEG;
            vel_display = vel_raw_rad_per_s * RAD_TO_DEG;
        }
        else
        {
            // 线性电机：通过导程换算为直线位移：位移(mm) = 总角度(rad) * 导程(mm) / (2π)
            pos_display = total_angle_rad * RAD_TO_LINEAR_MM;

            // 速度同样换算：rad/s → mm/s
            vel_display = vel_raw_rad_per_s * RAD_TO_LINEAR_MM;
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
        // 轴4（线性电机）力矩显示有问题，暂时置0
        if (i == 3)
            g_shm_data->torque_nm[i] = 0.0;
        else
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

int ServoDemo()
{
    const char* robot_name = get_name_robot_device(get_deviceName(0, NULL), 0);
    get_control_cycle(get_deviceName(0, NULL));

    // 初始化共享内存
    if (init_shared_memory() != 0)
    {
        printf("警告: 共享内存初始化失败，Qt监控界面将无法连接\n");
    }

    // 1. 使能所有三个电机 (速度模式)
    printf("1. 使能所有 %d 个电机 (axis_ID: 1, 2, 3, 4)...\n", NUM_AXES);
    for (int i = 0; i < NUM_AXES; i++)
    {
        enable_axis(robot_name, g_axis_ids[i]);
    }

    printf("\n=== 慢速位置控制 ===\n");
    printf("输入格式: 轴号 位置量\n");
    printf("例如: 1 90  表示轴1转动90度 (旋转电机)\n");
    printf("      4 50  表示轴4移动50毫米 (线性电机，导程5mm)\n");
    printf("按回车回到原位，然后可继续输入\n\n");

    // 各轴慢速参数（显示单位/秒）
    // 旋转电机：deg/s，线性电机：mm/s
    double slow_speed_rotary = 5.0;     // 旋转电机：5 deg/s
    double slow_speed_linear = 5.0;     // 线性电机：5 mm/s

    while (robot_ok())
    {
        int axis_id = 0;
        double input_value = 0.0;

        // 在等待用户输入时，持续更新共享内存数据（保持曲线刷新）
        update_motor_data(robot_name);

        // 读取用户输入
        printf("请输入 轴号(1/2/3/4) 位置量: ");
        fflush(stdout);
        int ret = scanf("%d %lf", &axis_id, &input_value);
        if (ret != 2)
        {
            printf("输入格式错误，请重新输入\n");
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }

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
        if (!valid)
        {
            printf("无效轴号，请输入 1、2、3 或 4\n");
            continue;
        }

        const char* unit = unit_str(axis_id);

        // 根据电机类型选择速度
        double speed = (g_axis_types[axis_idx] == MOTOR_TYPE_LINEAR)
                       ? slow_speed_linear : slow_speed_rotary;

        // 从共享内存读取当前位置（已包含 unwrap 和单位换算）
        double pos_start = g_shm_data->position_deg[axis_idx];
        printf("  轴 %d 当前位置: %.2f %s\n", axis_id, pos_start, unit);

        // 计算目标位置（相对运动）
        double target_position = pos_start + input_value;
        printf("  轴 %d 目标位置: %.2f %s (速度: %.1f %s/s)\n",
               axis_id, target_position, unit, speed, unit);

        // 慢速运动到目标位置
        move_to_position_slow(robot_name, axis_id, target_position, speed);

        // 等待用户按回车回到原位
        printf("\n按回车键回到原位...\n");
        fflush(stdout);
        // 清空输入缓冲区中 scanf 残留的字符（包括换行符）
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        // 再次阻塞等待用户输入回车
        while ((c = getchar()) != '\n' && c != EOF);

        // 慢速回到起始位置
        printf("  轴 %d 回到原位...\n", axis_id);
        move_to_position_slow(robot_name, axis_id, pos_start, speed);

        printf("\n");
    }

    printf("\n演示结束\n");

    // 清理共享内存
    cleanup_shared_memory();

    return 0;
}

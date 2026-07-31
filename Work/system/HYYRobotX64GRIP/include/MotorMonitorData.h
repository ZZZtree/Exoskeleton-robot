#ifndef MOTOR_MONITOR_DATA_H
#define MOTOR_MONITOR_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 共享内存名称
 */
#define MOTOR_SHM_NAME "/hyy_motor_monitor_shm"

/**
 * @brief 共享内存大小（字节）
 */
#define MOTOR_SHM_SIZE (sizeof(MotorMonitorData))

/**
 * @brief 电机数量
 */
#define MOTOR_NUM_AXES 10

/**
 * @brief 电机类型：0=旋转电机（角度），1=线性电机（毫米）
 */
#define MOTOR_TYPE_ROTARY  0
#define MOTOR_TYPE_LINEAR  1

/**
 * @brief 控制命令类型
 */
#define CMD_NONE            0   // 无命令
#define CMD_MOVE_TO         1   // 相对运动（位置模式，到目标位置后停止）
#define CMD_STOP            2   // 停止电机
#define CMD_ENABLE          3   // 使能电机
#define CMD_DISABLE         4   // 失能电机
#define CMD_MOVE_ABS        5   // 绝对运动（位置模式，到目标位置后停止）
#define CMD_VELOCITY_MODE   6   // 速度模式：以指定速度持续运动（正=正向，负=反向）
#define CMD_VELOCITY_STOP   7   // 停止速度模式运动
#define CMD_SEQ_START       8   // 启动动作序列队列（循环执行）
#define CMD_SEQ_STOP        9   // 停止动作序列队列
#define CMD_SEQ_ADD_GROUP   10  // 添加一组动作到队列
#define CMD_SEQ_CLEAR       11  // 清空动作序列队列
#define CMD_SEQ_DELETE_GROUP 12 // 删除队列中指定组

/** 动作序列队列最大组数 */
#define MAX_SEQUENCE_GROUPS 20

/**
 * @brief 共享内存中的电机监控数据结构
 *
 * 机器人控制程序（ServoDemo）每10ms写入一次数据，
 * Qt 界面程序每50ms读取一次并更新曲线。
 *
 * Qt 界面可通过写入 cmd 字段发送控制指令，
 * ServoDemo 在控制循环中检查并执行。
 */
typedef struct {
    // ========== 监控数据（ServoDemo 写入，Qt 读取） ==========

    /** 时间戳（微秒，相对于程序启动） */
    int64_t timestamp_us;

    /** 序列号，用于检测数据更新 */
    uint64_t sequence;

    /** 电机类型（0=旋转，1=线性） */
    int32_t axis_type[MOTOR_NUM_AXES];

    /** 电机的位置（旋转电机：度，线性电机：毫米） */
    double position_deg[MOTOR_NUM_AXES];

    /** 电机的速度（旋转电机：度/秒，线性电机：毫米/秒） */
    double velocity_deg_per_s[MOTOR_NUM_AXES];

    /** 电机的力矩/力传感器数据（旋转电机：Nm，线性电机：N） */
    double torque_nm[MOTOR_NUM_AXES];

    /** 电机使能状态 */
    int32_t power_status[MOTOR_NUM_AXES];

    /** 电机错误状态 */
    int32_t error_status[MOTOR_NUM_AXES];

    /** 控制周期（微秒） */
    int32_t control_cycle_us;

    // ========== 单轴控制请求（Qt 写入，ServoDemo 读取并执行） ==========
    // 用于速度模式、使能/失能等单轴操作

    /** 控制命令类型（CMD_xxx） */
    int32_t cmd_type;

    /** 目标轴索引 (0~9) */
    int32_t cmd_axis_idx;

    /** 通用数值参数（旋转电机：度，线性电机：毫米）
     *  位置模式(CMD_MOVE_TO/CMD_MOVE_ABS)：目标位置
     *  速度模式(CMD_VELOCITY_MODE)：速度值（正=正向，负=反向） */
    double cmd_value;

    /** 运动速度（旋转电机：度/秒，线性电机：毫米/秒） */
    double cmd_speed;

    /** 位置模式的目标位置（绝对位置，用于判断何时停止） */
    double cmd_target_position;

    /** 控制命令序列号（Qt 每次写入递增，ServoDemo 执行后更新为相同值表示已执行） */
    uint64_t cmd_sequence;

    /** 控制命令执行状态：0=空闲，1=正在执行，2=执行完成，-1=执行失败 */
    int32_t cmd_status;

    /** 命令执行结果描述 */
    char cmd_result[64];

    // ========== 多轴控制请求（Qt 写入，ServoDemo 读取并执行） ==========
    // 用于同时控制多个电机的位置模式运动

    /** 多轴运动状态：0=空闲，1=正在执行多轴运动 */
    int32_t multi_cmd_status;

    /** 多轴命令序列号（Qt 每次写入递增） */
    uint64_t multi_cmd_sequence;

    /** 每个轴是否参与本次多轴运动（0=不参与，1=参与） */
    int32_t multi_active[MOTOR_NUM_AXES];

    /** 每个轴的目标位置（绝对位置） */
    double multi_target_pos[MOTOR_NUM_AXES];

    /** 每个轴的运动速度 */
    double multi_speed[MOTOR_NUM_AXES];

    /** 每个轴的执行状态：0=等待，1=运动中，2=已到达，-1=失败 */
    int32_t multi_axis_status[MOTOR_NUM_AXES];

    /** 多轴命令执行结果描述 */
    char multi_cmd_result[256];

    // ========== 动作序列队列（Qt 写入，ServoDemo 读取并执行） ==========
    // Qt 界面将多组动作存入队列，ServoDemo 按顺序循环执行

    /** 序列队列中实际组数 (0 ~ MAX_SEQUENCE_GROUPS) */
    int32_t seq_group_count;

    /** 序列队列命令序列号（Qt 写入递增，用于通知 ServoDemo 有更新） */
    uint64_t seq_cmd_sequence;

    /** 序列总执行状态：0=空闲，1=运行中，2=暂停，-1=错误 */
    int32_t seq_status;

    /** 当前正在执行的组号 (0-based，0 表示第 1 组) */
    int32_t seq_current_group;

    /** 是否循环执行：0=单次（执行完所有组后停止），1=循环（回到第 1 组继续） */
    int32_t seq_looping;

    /** 每组动作中每个轴是否参与（0=不参与，1=参与） */
    int32_t seq_active[MAX_SEQUENCE_GROUPS][MOTOR_NUM_AXES];

    /** 每组动作中每个轴的目标位置（绝对位置） */
    double seq_target[MAX_SEQUENCE_GROUPS][MOTOR_NUM_AXES];

    /** 每组动作中每个轴的运动速度 */
    double seq_speed[MAX_SEQUENCE_GROUPS][MOTOR_NUM_AXES];

    /** 当前组每个轴的执行状态：0=等待，1=运动中，2=已到达，3=限位触发，-1=失败 */
    int32_t seq_axis_status[MOTOR_NUM_AXES];

    /** 序列执行状态描述 */
    char seq_result[256];

} MotorMonitorData;

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_MONITOR_DATA_H */

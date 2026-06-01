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
#define MOTOR_NUM_AXES 4

/**
 * @brief 电机类型：0=旋转电机（角度），1=线性电机（毫米）
 */
#define MOTOR_TYPE_ROTARY  0
#define MOTOR_TYPE_LINEAR  1

/**
 * @brief 共享内存中的电机监控数据结构
 *
 * 机器人控制程序（ServoDemo）每10ms写入一次数据，
 * Qt 界面程序每50ms读取一次并更新曲线。
 */
typedef struct {
    /** 时间戳（微秒，相对于程序启动） */
    int64_t timestamp_us;

    /** 序列号，用于检测数据更新 */
    uint64_t sequence;

    /** 电机类型（0=旋转，1=线性） */
    int32_t axis_type[MOTOR_NUM_AXES];

    /** 四个电机的位置（旋转电机：度，线性电机：毫米） */
    double position_deg[MOTOR_NUM_AXES];

    /** 四个电机的速度（旋转电机：度/秒，线性电机：毫米/秒） */
    double velocity_deg_per_s[MOTOR_NUM_AXES];

    /** 四个电机的力矩/力传感器数据（旋转电机：Nm，线性电机：N） */
    double torque_nm[MOTOR_NUM_AXES];

    /** 电机使能状态 */
    int32_t power_status[MOTOR_NUM_AXES];

    /** 电机错误状态 */
    int32_t error_status[MOTOR_NUM_AXES];

    /** 控制周期（微秒） */
    int32_t control_cycle_us;

} MotorMonitorData;

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_MONITOR_DATA_H */

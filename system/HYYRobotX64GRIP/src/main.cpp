/*
 * main.cpp
 *
 *  Created on: 2022-9-20
 *      Author: HanBing
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include "HYYRobotInterface.h"
#include "user/BscanServer.h"
using namespace HYYRobotBase;
extern int SokcetDemo();
extern void ServoDemo();

/**
 * @brief STO（Safe Torque Off）数据项描述
 */
typedef struct {
    int slave_id;       ///< EtherCAT从站编号
    const char* name;   ///< PDO数据名称
} StoItem;

/**
 * @brief 所有TK3模块的STO控制位列表
 *
 * 根据ENI配置（eni.xml），TK3 I/O模块（0x00000009/0x00009252）的
 * RxPDO 0x7000 SubIndex 1（Reserved1）对应STO控制位。
 *
 * Slave 0 → 轴1
 * Slave 2 → 轴3
 * Slave 4 → 轴5
 */
static const StoItem g_sto_items[] = {
    {0, "slave:0.pdo:7000,1.Reserved1"},   // 轴1 STO
    {2, "slave:2.pdo:7000,1.Reserved1"},   // 轴3 STO
    {4, "slave:4.pdo:7000,1.Reserved1"},   // 轴5 STO
};
static const int g_sto_count = sizeof(g_sto_items) / sizeof(g_sto_items[0]);

/**
 * @brief 设置所有STO（Safe Torque Off）为高电平，使能所有驱动器输出
 *
 * 遍历所有TK3 I/O模块（slave 0, 2, 4），
 * 通过set_custom_device_data()写入Reserved1为1，
 * 对应ENI配置中的STO控制位。
 *
 * @return int 成功写入的STO数量; -1:全部失败
 */
static int SetStoHigh()
{
    const char* device_name = HYYRobotBase::get_deviceName(0, NULL);
    if (NULL == device_name)
    {
        printf("get_deviceName失败，无法写入STO。\n");
        return -1;
    }

    uint8_t sto_high = 1;
    int success_count = 0;

    for (int i = 0; i < g_sto_count; i++)
    {
        const char* data_name = g_sto_items[i].name;
        int ret = HYYRobotBase::set_custom_device_data(device_name, data_name, &sto_high, sizeof(sto_high));
        printf("set_custom_device_data写STO: device=%s, slave=%d, data_name=%s, ret=%d\n",
               device_name, g_sto_items[i].slave_id, data_name, ret);
        if (ret == 0)
            success_count++;
    }

    printf("STO写入完成: 成功 %d/%d\n", success_count, g_sto_count);
    return success_count;
}

int main(int argc, char *argv[])
{
	//------------------------initialize----------------------------------
	int err=0;
	HYYRobotBase::command_arg arg;
	err=HYYRobotBase::commandLineParser(argc, argv,&arg);
	if (0!=err)
	{
		return -1;
	}
	err=HYYRobotBase::system_initialize(&arg);
	if (0!=err)
	{
		return err;
	}
	//-----------------------user designation codes---------------
	// HYYRobotBase::DevicePower();
	// bscan_server::BscanServer bs;
	// IMPORTTOOL(tool10);
	// bs.StartBscanServer(100,tool10,NULL);

	// HYYRobotBase::DevicePoweroff();
	// ServoDemo();
	
	//------------------------两个电机正弦运动------------------------
	// 获取机器人设备名称（两个电机在同一个机器人设备上）
	SetStoHigh();
	fflush(stdout);
	ServoDemo();
	//------------------------wait----------------------------------
	pause();
	return 0;
}

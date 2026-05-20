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
#include "HYYRobotInterface.h"
#include "user/BscanServer.h"
using namespace HYYRobotBase;
extern int SokcetDemo();
extern void ServoDemo();
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

	ServoDemo();
	//------------------------wait----------------------------------
	pause();
	return 0;
}

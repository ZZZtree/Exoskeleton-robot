
#include "HYYRobotInterface.h"


extern int robot_runing();
int MainModule()
{
	move_start();//上电，使用move指令进行运动时必须使用该语句上电

	//获取数据

	//获取笛卡尔位置
	robpose rpose2;
	getrobpose("p2", &rpose2);
	//获取关节位置
	robjoint joint1;
	getrobjoint("j1",&joint1);
	//获取速度
	speed sp;
	getspeed("v1",&sp);
	//转弯区
	zone ze;
        getzone("z0",&ze);
	//获取工具
	tool to;
	gettool("tool0",&to);
	//获取工件
	wobj wo;
	getwobj("wobj0",&wo);
	//运动到直线的起点
	moveA(&joint1,&sp,NULL,NULL,NULL);


	//设置指令为线程运行方式
	setMoveThread(1);
	//以直线运动方式运动到p2点
	moveL(&rpose2,&sp,&ze,&to,&wo);
	Rsleep(1000);
	//采集直线运动期间关节位置（rad）和关节力矩（Nmm）

	//创建定时器
	RTimer mytimer;
	initUserTimer(&mytimer, 0, 1);

	double position[10];
	double torque[10];
	int robot_index=0;
	//获取索引robot_index的机器人名称
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL), robot_index);
	while(robot_runing())//机器人移动期间采集数据
	{
		//定时
		userTimer(&mytimer);

		//采集关节位置(rad)
		GetGroupPosition(robot_name, position);
		//采集关节力矩（Nmm）
		GetGroupTorque(robot_name,torque);
		//保存关节位置（rad）
		RSaveDataFast1("robot_position",1, 100, 6, position );
		//保存关节力矩（Nmm）
		RSaveDataFast1("robot_torque",1, 100, 6, torque );
	}

	move_stop();
    return 0;
}

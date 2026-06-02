#include "HYYRobotInterface.h"
int MainModule()
{
while(robot_ok())
{
	int ret=0;
	double target_position[10];
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
	int dof=get_group_dof(robot_name);
	double dt=get_control_cycle(get_deviceName(0,NULL));
	//创建读服务,打开文件 readtest.txt,内部存放按照控制周期离散的数
	CreateReadData1("drag_data", 1, 200, dof+1);
	Rsleep(2000);//1s,等待读数据服务准备好
	ret=RReadData("drag_data", target_position );//读一行数据
	IMPORTSPEED(v100);
	SETJOINT7(jinit,target_position[0],target_position[1],target_position[2],target_position[3],target_position[4],target_position[5],target_position[6]);
	MoveA(jinit,v100,NULL,NULL,NULL);//运动到初始位置
	RTimer timer;
	initUserTimer(&timer, 0, 1);
	TIIRFilters fangle;
	initTIIRFilters(&fangle, iirLPF, 100, 1.0/dt, dof);
	CreateGrip("grip");
	//循环读取数据并下发给设备
	while (robot_ok())
	{
		userTimer(&timer);
		ret=RReadData("drag_data", target_position );//读一行数据
		if (ret<0) //数据读完
		{
			printf("drag_data, ret=%d\n",ret);
			break;
		}
		IIRFilters(&fangle, target_position, target_position);//滤波
		SetGroupPosition(robot_name, target_position);
		if (2==target_position[dof])
		{
			ControlGrip("grip",1);
		}
		if (3==target_position[dof])
		{
			ControlGrip("grip",0);
		}
	}
	DestroyGrip("grip");
	DeleteReadData("drag_data");//删除读服务


	Rsleep(2000);

}
	return 0;
}

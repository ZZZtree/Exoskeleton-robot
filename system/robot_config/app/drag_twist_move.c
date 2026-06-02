
#include "HYYRobotInterface.h"



int MainModule()
{

	int ret=0;
	double target_position[15];
	const char* robot_name_l=get_name_robot_device(get_deviceName(0,NULL),0);
	const char* robot_name_r=get_name_robot_device(get_deviceName(0,NULL),1);
	int n=14;
	double dt=get_control_cycle(get_deviceName(0,NULL));
	//创建读服务,打开文件 readtest.txt,内部存放按照控制周期离散的数
	while(robot_ok())
	{
	CreateReadData1("drag_data", 1, 200, n+1);
	Rsleep(2000);//1s,等待读数据服务准备好
	ret=RReadData("drag_data", target_position );//读一行数据
	IMPORTSPEED(v100);
	SETJOINT7(jinit_l,target_position[0],target_position[1],target_position[2],target_position[3],target_position[4],target_position[5],target_position[6]);
	SETJOINT7(jinit_r,target_position[7],target_position[8],target_position[9],target_position[10],target_position[11],target_position[12],target_position[13]);
	DualMoveA(jinit_l,jinit_r,v100,v100,NULL3,NULL3);//运动到初始位置
	RTimer timer;
	initUserTimer(&timer, 0, 1);
	TIIRFilters fangle_l;
	TIIRFilters fangle_r;
	initTIIRFilters(&fangle_l, iirLPF, 100, 1.0/dt, 7);
	initTIIRFilters(&fangle_r, iirLPF, 100, 1.0/dt, 7);
	SetDo(0,1);
	SetDo(16,1);
	Rsleep(50);
	SetDo(0,0);
	SetDo(16,0);
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
		IIRFilters(&fangle_l, target_position, target_position);//滤波
		IIRFilters(&fangle_r, &(target_position[7]), &(target_position[7]));//滤波
		SetGroupPosition(robot_name_l, target_position);
		SetGroupPosition(robot_name_r, &(target_position[7]));
		if (2==target_position[n])
		{
			SetDo(1,1);
        		SetDo(17,1);
        		Rsleep(50);
        		SetDo(1,0);
        		SetDo(17,0);	
}
		if (3==target_position[n])
		{
			SetDo(0,1);
        		SetDo(16,1);
        		Rsleep(50);
        		SetDo(0,0);
        		SetDo(16,0);
		}
	}
	DeleteReadData("drag_data");//删除读服务


	Rsleep(100);

	}
	
	return 0;
}

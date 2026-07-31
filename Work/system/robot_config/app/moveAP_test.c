
#include "HYYRobotInterface.h"
int MainModule()
{
	printf("moveap_test start\n");
	double data[19];
	int num=0;
	CreateReadData1("aptest", 1, 100, 19);
	sleep(1);
	while (!RReadData("aptest", data))
	{
		usleep(1000);
		printf("PushMoveAPWayPoint\n");
		num=moveAP_push_way_point(data[0], &(data[1]),&(data[7]), &(data[13]));
		printf("num=%d\n",num);
	}
	printf("read end\n");
	setMoveThread(1);
	int ret=moveAP();
	RTimer timer;
	initUserTimer(&timer, 0, 1);
	double data_out[19];
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
	double dt=get_control_cycle(get_deviceName(0,NULL));
	double time=0;
	if (0!=CreateSaveData1("aptestout",1,100,19))
	{
		printf("CreateSaveData failure\n");
		return;
	}
	while (robot_runing())
	{
		userTimer(&timer);
		data_out[0]=time;
		GetGroupPosition(robot_name, &(data_out[1]));
		GetGroupTargetPosition(robot_name,  &(data_out[7]));
		GetGroupTorque(robot_name,  &(data_out[13]));
		time +=dt;
		RSaveData("aptestout", data_out );
	}

	printf("ret=%d\n",ret);
	printf("moveap_test end\n");

   	return 0;
}

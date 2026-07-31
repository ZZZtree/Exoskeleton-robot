
#include "HYYRobotInterface.h"
int MainModule()
{
	printf("movel_test start\n");
	
	IMPORTJOINT(j10);
	IMPORTPOSE(p11);
	IMPORTSPEED(v100);
	moveA(j10,v100,NULL,NULL,NULL);

	setMoveThread(1);
	int ret=moveL(p11,v100,NULL,NULL,NULL);
	RTimer timer;
	initUserTimer(&timer, 0, 1);
	double data_out[19];
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
	double dt=get_control_cycle(get_deviceName(0,NULL));
	double time=0;
	if (0!=CreateSaveData1("ltestout",1,100,19))
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
		RSaveData("ltestout", data_out );
	}

	printf("ret=%d\n",ret);
	printf("movel_test end\n");

   	return 0;
}

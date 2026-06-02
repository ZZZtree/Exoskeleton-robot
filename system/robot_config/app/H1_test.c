
#include "HYYRobotInterface.h"
int MainModule()
{
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),1);
	//axis_power_off(robot_name,2);
	//axis_power_off(robot_name,3);
	Rsleep(1000);
        IMPORTJOINT(jl1);
	IMPORTJOINT(jr1);
	IMPORTPOSE(pl1);
	IMPORTPOSE(pl2);
	IMPORTPOSE(pl3);
	IMPORTPOSE(pr1);
	IMPORTPOSE(pr2);
	IMPORTPOSE(pr3);
	IMPORTSPEED(v200);
	IMPORTSPEED(v100);
	DualMoveA(jl1,jr1,v200,v200,NULL3,NULL3);
	//DualMoveA(jl1,jr1,v100,v100,NULL3,NULL3);

	while(robot_ok())
	{
		DualMoveL(pl2,pr2,v200,v200,NULL3,NULL3);
		DualMoveL(pl3,pr3,v200,v200,NULL3,NULL3);
		DualMoveL(pl1,pr1,v200,v200,NULL3,NULL3);
	}

	Rsleep(1000);
    return 0;
}

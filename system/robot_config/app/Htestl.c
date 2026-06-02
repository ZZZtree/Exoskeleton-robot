
#include "HYYRobotInterface.h"
int MainModule()
{
        IMPORTJOINT(jl1);
	IMPORTJOINT(jr1);
	IMPORTPOSE(pl1);
	IMPORTPOSE(pl2);
	IMPORTPOSE(pl3);
	IMPORTPOSE(pr1);
	IMPORTPOSE(pr2);
	IMPORTPOSE(pr3);
	IMPORTSPEED(v200);
	MoveA(jl1,v200,NULL3);
	while(robot_ok())
	{
		MoveL(pl2,v200,NULL3);
		MoveL(pl3,v200,NULL3);
		MoveL(pl1,v200,NULL3);
	}

	Rsleep(1000);
    return 0;
}


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
	MultiMoveA(jl1,v200,NULL3,1);
	while(robot_ok())
	{
		MultiMoveL(pl2,v200,NULL3,1);
		MultiMoveL(pl3,v200,NULL3,1);
		MultiMoveL(pl1,v200,NULL3,1);
	}

	Rsleep(1000);
    return 0;
}


#include "HYYRobotInterface.h"
int MainModule()
{
	IMPORTJOINT(jl1);
	IMPORTJOINT(jl2);
	IMPORTJOINT(jl3);
	IMPORTJOINT(jr1);
	IMPORTJOINT(jr2);
	IMPORTJOINT(jr3);
	IMPORTSPEED(v200);
	IMPORTSPEED(v1000);
	DualMoveA(jl1,jr1,v200,v200,NULL3,NULL3);
	//DualMoveA(jl1,jr1,v100,v100,NULL3,NULL3);

	while(robot_ok())
	{
		DualMoveA(jl2,jr2,v1000,v1000,NULL3,NULL3);
		DualMoveA(jl3,jr3,v1000,v1000,NULL3,NULL3);
		//DualMoveA(pl1,pr1,v1000,v1000,NULL3,NULL3);
	}

	Rsleep(1000);
    return 0;
}

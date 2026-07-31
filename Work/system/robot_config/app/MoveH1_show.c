
#include "HYYRobotInterface.h"


void* add_move(void* arg)
{
	SETSPEEDTIME(vel,2);
	SETJOINT2(wj101,0.2,0.2);
	SETJOINT2(wj102,-0.2,-0.2);
	while (addition_ok())
	{
		MultiMoveAdd(wj101, vel, NULL3,0);
		MultiMoveAdd(wj102, vel, NULL3,0);
	}
	return NULL;
}

void* add_move1(void* arg)
{
        SETSPEEDTIME(vel,2);
	SETJOINT2(hj101,1.0,0.2);
	SETJOINT2(hj102,-1.0,-0.2);
        while (addition_ok())
        {
                MultiMoveAdd(hj101, vel, NULL3,1);
                MultiMoveAdd(hj102, vel, NULL3,1);
        }
	return NULL;
}



int MainModule()
{
	SETSPEEDTIME(vel,2);
        IMPORTJOINT(lj101);
        IMPORTJOINT(lj102);
        IMPORTJOINT(rj101);
        IMPORTJOINT(rj102);
	Rsleep(1000);
	int ret=ThreadCreat(add_move, NULL, "add_move", 1);
	if (0!=ret)
	{
		printf("ThreadCreat\n");
		return 0;
	}
	ret=ThreadCreat(add_move1, NULL, "add_move1", 1);
        if (0!=ret)
        {
                printf("ThreadCreat\n");
                return 0;
        }

	while (robot_ok())
	{	
		DualMoveA(lj101,rj101, vel,vel, NULL3,NULL3);
		DualMoveA(lj102,rj102, vel,vel, NULL3,NULL3);
	}
	Rsleep(1000);
	ThreadDataFree("add_move");
	ThreadDataFree("add_move1");
	return 0;
}

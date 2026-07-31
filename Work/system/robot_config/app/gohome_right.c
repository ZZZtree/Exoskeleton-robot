
#include "HYYRobotInterface.h"
int MainModule()
{
        SETJOINT7(home,0,0,0,0,0,0,0);
	IMPORTSPEED(v10);
	MultiMoveA(home,v10,NULL,NULL,NULL,1);
	Rsleep(1000);
    return 0;
}

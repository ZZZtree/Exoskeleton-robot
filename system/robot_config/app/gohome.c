
#include "HYYRobotInterface.h"
int MainModule()
{
        SETJOINT7(home,0,0,0,0,0,0,0);
	IMPORTSPEED(v100);
	DualMoveA(home,home,v100,v100,NULL3,NULL3);
	Rsleep(1000);
    return 0;
}

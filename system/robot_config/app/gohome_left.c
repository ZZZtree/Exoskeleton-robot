
#include "HYYRobotInterface.h"
int MainModule()
{
    SETJOINT7(home,0,0.2,0,0.2,0,0.3,0);
	printf("==%f\n",home->angle[1]);
	IMPORTSPEED(v10);
	int ret=MultiMoveA(home,v10,NULL,NULL,NULL,0);
	printf("%d\n",ret);
	Rsleep(1000);
    return 0;
}

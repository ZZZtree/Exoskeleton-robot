
#include "HYYRobotInterface.h"
int MainModule()
{
	IMPORTJOINT(j102);
	IMPORTPOSE(p102);
	IMPORTPOSE(p103);
	IMPORTSPEED(v10);
	//IMPORTTOOL(tool2);
	//IMPORTWOBJ(wobj666);


	moveA(j102,v10,NULL,NULL,NULL);
	moveL(p103,v10,NULL,NULL,NULL);

    return 0;
}

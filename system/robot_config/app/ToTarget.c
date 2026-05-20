
#include "HYYRobotInterface.h"
int MainModule()
{

	IMPORTSPEED(v10);
	IMPORTJOINT(jw12);
	MultiMoveA(jw12,v10,NULL,NULL,NULL,1);
	
	Rsleep(2000);    
return 0;
}

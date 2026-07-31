
#include "HYYRobotInterface.h"
int MainModule()
{
	robjoint j0;
	getrobjoint("j100", &j0);
	speed sp;
	getspeed("v10",&sp);

	move_start();

	moveA(&j0,&sp,NULL,NULL,NULL);
	
	move_stop();
    return 0;
}

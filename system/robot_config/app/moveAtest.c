
#include "HYYRobotInterface.h"
int MainModule()
{
/*
	robjoint j0;
	double data[6]={0.1,0.5,-1,0.8,-0.3,-0.8};
	init_robjoint(&j0, data, 6);
	//getrobjoint("j0", &j0);
	speed sp;
	getspeed("v1",&sp);
*/
	move_start();

	SETJOINT6(jtest,0.1,0.5,-1,0.8,-0.3,-0.8);
	SETSPEED(sp,0.2,100);
	moveA(jtest,sp,NULL,NULL,NULL);
	

	IMPORTJOINT(j0);
	IMPORTSPEED(v100);
	IMPORTTOOL(tool0);
	moveA(j0,v100,NULL,tool0,NULL);

	sleep(2);
	move_stop();
    return 0;
}

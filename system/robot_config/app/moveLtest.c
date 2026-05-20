
#include "HYYRobotInterface.h"
int MainModule()
{
	robjoint j0;
	getrobjoint("j10", &j0);
	robpose p1;
	getrobpose("p11",&p1);
	robpose p2;
	getrobpose("p12",&p2);
	speed sp;
	getspeed("v1",&sp);
	zone zo;
	getzone("z3",&zo);
	move_start();
printf("moveA\n");
	moveA(&j0,&sp,NULL,NULL,NULL);
printf("moveL\n");
	moveL(&p1,&sp,&zo,NULL,NULL);
printf("moveL\n");
	moveL(&p2,&sp,NULL,NULL,NULL);
	sleep(2);
	move_stop();

    return 0;
}

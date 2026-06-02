
#include "HYYRobotInterface.h"
int MainModule()
{
	//上电，使用move指令进行运动时必须使用该语句上电
	move_start();

	//获取数据

	speed sp;
	getspeed("v1",&sp);


	moveS("moveS.txt",&sp,NULL,NULL);

	//下电，使用move指令进行运动时必须使用该语句下电
	move_stop();
    return 0;
}

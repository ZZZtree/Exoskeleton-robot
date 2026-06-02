
#include "HYYRobotInterface.h"
int MainModule()
{
	//上电，使用move指令进行运动时必须使用该语句上电
	move_start();
	Rsleep(1000);
	//获取数据
	IMPORTJOINT(jc1);
	IMPORTPOSE(pc1);
	IMPORTPOSE(pc2);
	IMPORTPOSE(pc3);
	IMPORTSPEED(v100);
	moveA(jc1,v100,NULL,NULL,NULL);
	while (robot_ok())
	{
		moveL(pc2,v100,NULL,NULL,NULL);
		moveL(pc3,v100,NULL,NULL,NULL);
		moveL(pc1,v100,NULL,NULL,NULL);
	}
	Rsleep(1000);
	move_stop();   
	return 0;
}


#include "HYYRobotInterface.h"
int MainModule()
{
	//上电，使用move指令进行运动时必须使用该语句上电
	move_start();
	
	SETPOSE(rpose,285.0,237.0,333.0,-2.07,-0.305,0.19);
	SETPOSE(rpose1,255.0,-237.0,233.0,-2.27,-0.805,0.59);
	SETJOINT6(JI,0,0,0,0,1,0);
	//获取数据
	SETSPEEDPER(sp,0.1);

	setMoveThread(1);

	int ret=moveD(rpose,sp,NULL,NULL,0);

	Rsleep(1000);
	set_moveD_target(rpose1);

	Rsleep(1000);
	set_moveD_target(rpose);

	Rsleep(100000);
	setMoveThread(0);
	moveA(JI,sp,NULL,NULL,NULL);
	//下电，使用move指令进行运动时必须使用该语句下电
	move_stop();
    return 0;
}

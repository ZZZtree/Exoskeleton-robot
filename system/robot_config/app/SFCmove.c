
#include "HYYRobotInterface.h"
int MainModule()
{
	IMPORTPOSE(p2);
	IMPORTPOSE(p3);
	IMPORTJOINT(j1);
	IMPORTSPEED(v100);
	IMPORTZONE(z0);
	IMPORTTOOL(tool0);
	IMPORTWOBJ(wobj0);

	moveA(j1,v100,NULL,NULL,NULL);

	//初始化力控制
	int ret=SFCInit("SFCtest", 1, 0, tool0, wobj0, 0);
	printf("SFCInit,ret=%d\n",ret);
	
	//设置目标力
	double target_force[6]={0,0,5,0,0,0};
	ret=SFCSetHybridForceMotionTargetForce("SFCtest", target_force);
	Rdebug("SFCSetHybridForceMotionTargetForce,ret=%d\n",ret);
	double P[6]={0.01,0.01,0.01,0.01,0.01,0.01};
	double I[6]={0.001,0.001,0.001,0.001,0.001,0.001};
	double D[6]={0,0,0,0,0,0};
	ret=SFCSetHybridForceMotionCtrlParam("SFCtest", P, I, D);
	//开启力控制
	ret=SFCStart("SFCtest");
	Rdebug("SFCStart,ret=%d\n",ret);
	Rsleep(1000000);
	//moveL(&rpose2,&sp,&ze,&to,&wo);
	printf("1111111111\n");
	ret=SFCEnd("SFCtest");
	Rdebug("SFCEnd,ret=%d\n",ret);


	move_stop();
    return 0;
}


#include "HYYRobotInterface.h"
int MainModule()
{
	IMPORTPOSE(p2);
	IMPORTPOSE(p3);
	IMPORTJOINT(j1);
	IMPORTSPEED(v10);
	IMPORTZONE(z0);
	IMPORTTOOL(tool10);
	IMPORTWOBJ(wobj0);

	moveA(j1,v10,NULL,NULL,NULL);

	//Rsleep(3000);
	//初始化力控制
	int ret=SFCInit("SFCtest", 0, 0, tool10, wobj0, 0);
	printf("SFCInit,ret=%d\n",ret);
	
	double M[6]={10,10,10,1,1,1};
        double B[6]={400,400,400,10,10,10};
        double K[6]={100,100,100,2,2,2};
	

	//double M[6]={100,100,100,10,10,10};
        //double B[6]={2000,2000,2000,632,632,632};
        //double K[6]={10000,10000,100,10000,10000,10000};


	//double M[6]={10,10,10,1,1,1};
	//double B[6]={632,632,632,6.324,6.324,6.324};
	//double K[6]={10000,10000,10000,10,10,10};
	ret=SFCSetAdmittanceCtrlParam("SFCtest", M, B, K);
	printf("SFCSetAdmittanceCtrlParam,ret=%d\n",ret);
	//double target_force[6]={0,0,5,0,0,0};
	//SFCSetTargetForce("SFCtest", target_force);


	SFCSetMoveSpeedLimit("SFCtest", 1, 2);
	//开启力控制
	ret=SFCStart("SFCtest");


	EGMCreate_c("egmapp", 0, 0, 1, 4, tool10, wobj0);
        EGMRunJoint_c("egmapp", 1);
        EGMRelease_c("egmapp");



	ret=SFCEnd("SFCtest");
	printf("SFCEnd,ret=%d\n",ret);

    return 0;
}

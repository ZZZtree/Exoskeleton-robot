
#include "HYYRobotInterface.h"
int MainModule()
{
	IMPORTPOSE(p2);
	IMPORTPOSE(p3);
	IMPORTPOSE(p5);
	IMPORTPOSE(p6);
	IMPORTPOSE(p8);
	IMPORTJOINT(j1);
	IMPORTJOINT(j7);
	IMPORTSPEED(v10);
	IMPORTZONE(z0);
	IMPORTTOOL(tool10);
	IMPORTWOBJ(wobj0);

	moveA(j1,v10,NULL,NULL,NULL);
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
	//Rsleep(3000);
	//初始化力控制
	int ret=SFCInit("SFCtest", 0, 0, tool10, wobj0, 0);
	printf("SFCInit,ret=%d\n",ret);
	
	
	double M[6]={100,100,10,10,10,10};
        double B[6]={2000,2000,1000,632,632,632};
        double K[6]={10000,10000,0,10000,10000,10000};

	//double M[6]={100,100,100,10,10,10};
        //double B[6]={2000,2000,2000,632,632,632};
        //double K[6]={10000,10000,100,10000,10000,10000};


	//double M[6]={10,10,10,1,1,1};
	//double B[6]={632,632,632,6.324,6.324,6.324};
	//double K[6]={10000,10000,10000,10,10,10};
	ret=SFCSetAdmittanceCtrlParam("SFCtest", M, B, K);
	printf("SFCSetAdmittanceCtrlParam,ret=%d\n",ret);
	double target_force[6]={0,0,5,0,0,0};
	SFCSetTargetForce("SFCtest", target_force);
	//开启力控制
	ret=SFCStart("SFCtest");
	double tor[6];
	double angle[10];
	//TorqueSensorOpenBias(GetTorqueSensorName(0,NULL));
	printf("SFCStart,ret=%d\n",ret);
	//Rsleep(100000);
	//moveL(p8,v10,z0,tool10,wobj0);
	//moveL(p2,v10,z0,tool10,wobj0);
	//moveL(p3,v10,z0,tool10,wobj0);
	//moveL(p2,v10,z0,tool10,wobj0);
	//moveL(p3,v10,z0,tool10,wobj0);
	
	//moveL(p2,v10,z0,tool10,wobj0);
        while (robot_ok())
	{
		Rsleep(1000);
		GetGroupPosition(robot_name,angle);
		//GetSensorTorque(GetTorqueSensorName(0,NULL),  tor);
		GetSensorTorqueTransformToTool(GetTorqueSensorName(0,NULL), angle, tor);
		printf("%f,%f,%f,%f,%f,%f\n",tor[0],tor[1],tor[2],tor[3],tor[4],tor[5]);
	}
	//moveL(p3,v100,z0,tool0,wobj0);
	Rsleep(200000);
	ret=SFCEnd("SFCtest");
	printf("SFCEnd,ret=%d\n",ret);

    return 0;
}

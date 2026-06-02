
#include "HYYRobotInterface.h"



void* data_sample(void* arg)
{
	
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
	double tor[6];
        double angle[10];

 	while (robot_ok())
        {
                Rsleep(500);
                GetGroupPosition(robot_name,angle);
                //GetSensorTorque(GetTorqueSensorName(0,NULL),  tor);
                GetSensorTorqueTransformToTool(GetTorqueSensorName(0,NULL), angle, tor);
                printf("%f,%f,%f,%f,%f,%f\n",tor[0],tor[1],tor[2],tor[3],tor[4],tor[5]);
        }
	return NULL;
}



int MainModule()
{
	IMPORTPOSE(p2);
	IMPORTPOSE(p3);
	IMPORTPOSE(p5);
	IMPORTPOSE(p6);
	IMPORTPOSE(p8);
	IMPORTPOSE(p21);
	IMPORTPOSE(p22);
	IMPORTPOSE(p23);
	IMPORTPOSE(p24);
	IMPORTJOINT(j1);
	IMPORTJOINT(j7);
	IMPORTSPEED(v10);
	IMPORTSPEED(v100);
	IMPORTZONE(z0);
	IMPORTTOOL(tool10);
	IMPORTWOBJ(wobj0);
	
	moveA(j1,v100,NULL,NULL,NULL);
	while (robot_ok())
	{
	//Rsleep(3000);
	//初始化力控制
	int ret=SFCInit("SFCtest", 0, 0, tool10, wobj0, 0);
	printf("SFCInit,ret=%d\n",ret);
	
	
	double M[6]={10,10,10,10,10,1};
        double B[6]={1000,1000,1000,1000,5};
        double K[6]={100,100,0,10000,10000,2};

	//double M[6]={100,100,100,10,10,10};
        //double B[6]={2000,2000,2000,632,632,632};
        //double K[6]={10000,10000,100,10000,10000,10000};


	//double M[6]={10,10,10,1,1,1};
	//double B[6]={632,632,632,6.324,6.324,6.324};
	//double K[6]={10000,10000,10000,10,10,10};
	ret=SFCSetAdmittanceCtrlParam("SFCtest", M, B, K);
	printf("SFCSetAdmittanceCtrlParam,ret=%d\n",ret);
	double target_force[6]={0,0,10,0,0,0};
	SFCSetTargetForce("SFCtest", target_force);


	moveL(p22,v100,z0,tool10,wobj0);
        moveL(p21,v100,z0,tool10,wobj0);

	//开启力控制
	ret=SFCStart("SFCtest");
	


	ThreadCreat(data_sample, NULL, "fordata", 1);
	TorqueSensorOpenBias(GetTorqueSensorName(0,NULL));
	printf("SFCStart,ret=%d\n",ret);
	double is_valid[6]={0,0,1,0,0,0};
	robpose robpose_condition=Offs(p21, 0, 0, -0.015, 0, 0, 0);
        while (robot_ok())
	{
		if (SFCIsSatisfyForceCondition("SFCtest", target_force,is_valid,1,0)&&
			SFCIsSatisfyCartesianCondition("SFCtest", &robpose_condition,is_valid,0.005,0))
		{
			break;
		}
		moveL(p23,v10,z0,tool10,wobj0);
		moveL(p24,v10,z0,tool10,wobj0);
	}
	ret=SFCEnd("SFCtest");
	printf("SFCEnd,ret=%d\n",ret);
	
	robpose pout;
	GetCurrentCartesian(tool10,wobj0, &pout, 0);
	robpose pout1=Offs(&pout, 0, 0, 0.1, 0, 0, 0);
	moveL(&pout1,v10,z0,tool10,wobj0);
   	}
	return 0;
}


#include "HYYRobotInterface.h"

int flag=0;

void* data_sample(void* arg)
{
	
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
	double tor[6];
        double angle[10];
	RTimer timer;
	initUserTimer(&timer, 0, 1);
	Rsleep(1000);

 	while (robot_ok())
        {
                userTimer(&timer);
                GetGroupPosition(robot_name,angle);
                //GetSensorTorque(GetTorqueSensorName(0,NULL),  tor);
                GetSensorTorqueTransformToTool(GetTorqueSensorName(0,NULL), angle, tor);
                //printf("%f,%f,%f,%f,%f,%f\n",tor[0],tor[1],tor[2],tor[3],tor[4],tor[5]);
		//if (1==flag)
		{
			if (0!=RSaveDataFast1("ADMzp_test_data",1, 100, 6, tor ))
			{
				printf("data lost\n");
			}
		}
        }
	return NULL;
}



int MainModule()
{
	IMPORTPOSE(p31);
	IMPORTPOSE(p32);
	IMPORTJOINT(j1);
	IMPORTSPEED(v10);
	IMPORTSPEED(v100);
	IMPORTZONE(z0);
	IMPORTTOOL(tool10);
	IMPORTWOBJ(wobj0);
	
	moveA(j1,v100,NULL,NULL,NULL);
	//Rsleep(3000);
	//初始化力控制
	int ret=SFCInit("SFCtest", 0, 0, tool10, wobj0, 0);
	printf("SFCInit,ret=%d\n",ret);
	
	
	double M[6]={10,10,10,10,10,10};
        double B[6]={1000,1000,1000,1000,1000};
        double K[6]={10000,10000,0,10000,10000,10000};

	//double M[6]={100,100,100,10,10,10};
        //double B[6]={2000,2000,2000,632,632,632};
        //double K[6]={10000,10000,100,10000,10000,10000};


	//double M[6]={10,10,10,1,1,1};
	//double B[6]={632,632,632,6.324,6.324,6.324};
	//double K[6]={10000,10000,10000,10,10,10};
	ret=SFCSetAdmittanceCtrlParam("SFCtest", M, B, K);
	printf("SFCSetAdmittanceCtrlParam,ret=%d\n",ret);
	double target_force[6]={0,0,50,0,0,0};
	SFCSetTargetForce("SFCtest", target_force);


	moveL(p32,v100,z0,tool10,wobj0);
        moveL(p31,v100,z0,tool10,wobj0);

	//开启力控制
	ret=SFCStart("SFCtest");
	


	ThreadCreat(data_sample, NULL, "fordata", 1);
	TorqueSensorOpenBias(GetTorqueSensorName(0,NULL));
	printf("SFCStart,ret=%d\n",ret);
	printf("start record data\n");
	flag=1;
	Rsleep(30000);
	flag=0;
	printf("end recofe data\n");
	Rsleep(1000);
	ret=SFCEnd("SFCtest");
	printf("SFCEnd,ret=%d\n",ret);
	
	return 0;
}

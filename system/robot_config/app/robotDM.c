
#include "HYYRobotInterface.h"
int robot_work(double force,const char* cspeed, const char* czone, const char* ctool, const char* cwobj);
int MainModule()
{
	int ret=robot_work( 10,"v100", "z0", "tool15", "wobj0");//4dica的机器人模块
	return ret;
}

int robot_work(double force, const char* cspeed, const char* czone, const char* ctool, const char* cwobj)
{
	int ret=0;

	//获取运行数据
	speed sp;
	zone zo;
	tool to;
	wobj wo;
	getspeed(cspeed, &sp);
	getzone(czone, &zo);
	gettool(ctool, &to);
	getwobj(cwobj, &wo);
	IMPORTJOINT(j1)
	moveA(j1,&sp,NULL,NULL,NULL);
	sleep(3);
	//初始化力控制数据
	ret=SFCInit("robotOsPolish", 1, 1, &to, &wo, 0);
	if (0!=ret)
	{
		printf("robot_work: SFCInit failure\n");
		goto ROBOTWORKEND;
	}

	double target_force[6]={0,0,force,0,0,0};
	ret=SFCSetHybridForceMotionTargetForce("robotOsPolish", target_force);
	if (0!=ret)
	{
		printf("robot_work: SFCSetHybridForceMotionTargetForce failure\n");
		goto ROBOTWORKEND;
	}
	double M[6]={0,0,0.01,0,0,0};
	double B[6]={0,0,0.01,0,0,0};
	double coeff[6]={0,0,0,0,0,0};
	ret=SFCSetHybridForceMotionCtrlParam("robotOsPolish", M, B, coeff);
	if (0!=ret)
	{
		printf("robot_work: SFCSetHybridForceMotionCtrlParam failure\n");
		goto ROBOTWORKEND;
	}

	//运动
	//moveJ(&pstart, &sp, &zo, &to, &wo);//运动到其实点正上方
	//moveL(&(P[0]), &sp, &zo, &to, &wo);//开始运动

	//开启力控制
	ret=SFCStart("robotOsPolish");
	if (0!=ret)
	{
		printf("robot_work: SFCStart failure\n");
		goto ROBOTWORKEND;
	}
	
	
	printf("move\n");	
	
        Rsleep(100000);
	
	
	
	ret=SFCEnd("robotOsPolish");
	if (0!=ret)
	{
		printf("robot_work: SFCEnd failure\n");
		goto ROBOTWORKEND;
	}
	ROBOTWORKEND:
	return ret;
}





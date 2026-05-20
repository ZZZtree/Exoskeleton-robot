
#include "HYYRobotInterface.h"


void* add_move(void* arg)
{
	IMPORTSPEED(v100);
        SETJOINT2(aj1,0,0);
        SETJOINT2(aj2,-0.2,0.2);
        SETJOINT2(aj3,0,-0.2);
        SETJOINT2(aj4,-0.2,0.2);
        SETJOINT2(aj5,0,0.2);
/*
	while (robot_ok())
	{
		Rsleep(1000);
		printf("a1\n");
		MoveAdd(aj1, v100, NULL3);
		Rsleep(1000);
		printf("a2\n");
		MoveAdd(aj2, v100, NULL3);
		Rsleep(1000);
		printf("a3\n");
		MoveAdd(aj3, v100, NULL3);
		Rsleep(1000);
		printf("a4\n");
		MoveAdd(aj4, v100, NULL3);
		Rsleep(1000);
		printf("a5\n");
		MoveAdd(aj5, v100, NULL3);
	}
*/
	return NULL;
}

void* add_move1(void* arg)
{
        IMPORTSPEED(v100);
        SETJOINT2(aj1,0,0);
        SETJOINT2(aj2,0.1,0.1);
        SETJOINT2(aj3,0.1,-0.1);
        SETJOINT2(aj4,-0.1,0.1);
        SETJOINT2(aj5,0.1,0.1);
/*
        while (robot_ok())
        {
                Rsleep(1000);
                printf("a1\n");
                MultiMoveAdd(aj1, v100, NULL3,1);
                Rsleep(1000);
                printf("a2\n");
                MultiMoveAdd(aj2, v100, NULL3,1);
                Rsleep(1000);
                printf("a3\n");
                MultiMoveAdd(aj3, v100, NULL3,1);
                Rsleep(1000);
                printf("a4\n");
                MultiMoveAdd(aj4, v100, NULL3,1);
                Rsleep(1000);
                printf("a5\n");
                MultiMoveAdd(aj5, v100, NULL3,1);
        }
*/
        return NULL;
}



int MainModule()
{
	Rsleep(1000);
	//获取数据
	IMPORTJOINT(lj7);
	IMPORTJOINT(rj7);
	IMPORTPOSE(lp1);
	IMPORTPOSE(rp1);
	IMPORTPOSE(lp2);
        IMPORTPOSE(rp2);
	IMPORTPOSE(lp3);
        IMPORTPOSE(rp3);
	IMPORTPOSE(lp4);
        IMPORTPOSE(rp4);
	IMPORTPOSE(lp5);
        IMPORTPOSE(rp5);
	IMPORTPOSE(lp6);
        IMPORTPOSE(rp6);
	IMPORTPOSE(lp7);
        IMPORTPOSE(rp7);
	IMPORTSPEED(v100);
	IMPORTTOOL(tool0);
	IMPORTWOBJ(wobj1);
	SETJOINT2(aj1,0,0);
	MultiMoveAdd(aj1, v100, NULL3,1);
	MoveAdd(aj1, v100, NULL3);
	DualMoveA(lj7,rj7,v100,v100, NULL3,NULL3);
	int ret=0;

	ret=StartAdditionServer(tool0,wobj1,0);
	if (0!=ret)
	{
		printf("%d==%d\n",0,ret);
		return 0;
	}
        ret=StartAdditionServer(tool0,wobj1,1);
        if (0!=ret)
        {
                printf("%d==%d\n",1,ret);
                return 0;
        }

	Rsleep(1000);
	ret=ThreadCreat(add_move, NULL, "add_move", 1);
	if (0!=ret)
	{
		printf("ThreadCreat\n");
		return 0;
	}
	ret=ThreadCreat(add_move1, NULL, "add_move1", 1);
        if (0!=ret)
        {
                printf("ThreadCreat\n");
                return 0;
        }

	Rsleep(2000);
	while (robot_ok())
	{	
		printf("r1\n");
		DualMoveL(lp1,rp1, v100,v100, NULL,NULL, tool0,tool0, wobj1,wobj1);
		//Rsleep(10000);
		printf("r2\n");
		DualMoveL(lp2,rp2, v100,v100, NULL,NULL, tool0,tool0, wobj1,wobj1);
		printf("r3\n");
		DualMoveL(lp3,rp3, v100,v100, NULL,NULL, tool0,tool0, wobj1,wobj1);
		printf("r4\n");
		DualMoveL(lp4,rp4, v100,v100, NULL,NULL, tool0,tool0, wobj1,wobj1);
		printf("r5\n");
		DualMoveL(lp5,rp5, v100,v100, NULL,NULL, tool0,tool0, wobj1,wobj1);
		printf("r6\n");
		DualMoveL(lp6,rp6, v100,v100, NULL,NULL, tool0,tool0, wobj1,wobj1);
	}
	Rsleep(1000);
	StopAdditionServer(0);
	StopAdditionServer(1);
	ThreadDataFree("add_move");
	return 0;
}


#include "HYYRobotInterface.h"
#include <stdint.h>
#include <linux/input.h>
typedef struct InputEvent {
uint8_t isblock;
uint8_t value_range;//0:0值;1:检测负值;2:检测正值;3:检测所有值;
uint16_t type;
int fd;
}InputEvent;

extern int input_event_open(InputEvent* ie,const char* device_name,uint16_t type,uint8_t value_range,uint8_t isblock);

extern int input_event_read(InputEvent* ie,struct input_event* ev);

extern int input_event_close(InputEvent* ie);

void* read_fun(void* arg)
{
	double target[10]={0,0,0,0,0,0,0,0,0,0};
	RTimer mytimer;
	initUserTimer(&mytimer, 0, 1);
	const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
	int dof=get_group_dof(robot_name);
	 //InputEvent ie;
        //int ret=input_event_open(&ie,"/dev/input/event7",0x01,0,0);
        //printf("ret=%d\n",ret);
        //struct input_event ev;
	//CreateGrip ("grip");
	
	while (robot_ok())
	{
		userTimer(&mytimer);
		GetGroupPosition(robot_name,target);
		/*ret=input_event_read(&ie,&ev);
		if (0==ret)
		{
			target[dof]=ev.code;
			if (2==ev.code)
			{
				ControlGrip("grip",1);	
			}
			if(3==ev.code)
			{
				ControlGrip("grip",0);
			}
		}
		else
		{
			target[dof]=0;
		}*/
		RSaveDataFast1("drag_data",1, 100, dof+1, target );
	}
	return NULL;	
}
int MainModule()
{
	Rsleep(1000);
	IMPORTJOINT(j0523);
	IMPORTJOINT(j22);
	IMPORTSPEED(v50);
	IMPORTTOOL(tool10);
	IMPORTTOOL(tool0);
	//SETJOINT7(j21,j20->angle[0],j20->angle[1],j20->angle[2],j20->angle[3],j20->angle[4]+1.0,j20->angle[5],j20->angle[6]);
	MoveA(j0523,v50,NULL,NULL,NULL);

	Rsleep(1000);
	//初始化力控制
	int ret=JSFCInit("JSFCtest", 0,  tool0, NULL, 0);
	printf("JSFCInit,ret=%d\n",ret);
	

	//double K[7]={2,2.5,2,2.5,3,2.5,3};
	double K[7]={2,2,2,1.5,3,1.5,3};
	JSFCSetJointDragParam("JSFCtest", K);
	double te[7]={0.5,0.5,0.5,0.5,0.5,0.5,0.5};
	JSFCSetJointTranEfficParam("JSFCtest", te);

	JSFCSetSensorForceLimit("JSFCtest", 1);
	//开启力控制
	ret=JSFCStart("JSFCtest");
	printf("JSFCStart,ret=%d\n",ret);
	ret=ThreadCreat(read_fun,NULL,"read",1 );
	Rsleep(100000000);
	ret=JSFCEnd("JSFCtest");
	printf("JSFCEnd,ret=%d\n",ret);
	sleep(10);
	printf("END\n");
    return 0;
}

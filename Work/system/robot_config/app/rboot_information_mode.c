
#include "HYYRobotInterface.h"


extern int get_robot_power_state(int _index);
extern int get_move_state(int _index);
typedef struct TimeSlot{
	struct timeval start;
	struct timeval end;
}TimeSlot;
extern int gettimestart(TimeSlot* timeslot);
extern int gettimeend(TimeSlot* timeslot);
extern int gettimediff(TimeSlot* timeslot);

int MainModule()
{
	const char* opcuName="robot_mode";
	int err=0;//ClientCreate("192.168.0.250", 3360, opcuName);
    	if (0!=err)
	{
		return -1;
	}
	else
	{
		printf("opcua client create!\n");
	}
	char* d;
	char data[200];
	double joint[10];
	double joint_[10];
	double pospose[6];
	int powerdata=0;
	int powerflag=0;
	TimeSlot time;
	memset(data,0,sizeof(data));
	memset(joint,0,sizeof(joint));
	memset(joint_,0,sizeof(joint_));
	memset(pospose,0,sizeof(pospose));

	int opcuaUpateTime=10000; //10ms
	double opcuaUpataTime_s=(0.000001*opcuaUpateTime);

    //更新实时信息
    while(robot_ok())
    {
    	//power state
    	sprintf(data,"26,put:%d",get_robot_power_state(0));
    	SocketSendString(data, opcuName);
        usleep(opcuaUpateTime);
    	//move state
    	sprintf(data,"27,put:%d",get_move_state(0));
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
        //robot run time
   	if(1==get_robot_power_state(0))
    	{
    		gettimeend(&time);
    		int runtime = gettimediff(&time);
    		sprintf(data,"28,put:%d",runtime);
    		SocketSendString(data, opcuName);
    	}
    	else
    	{
    		sprintf(data,"28,put:%d",0);
    		SocketSendString(data, opcuName);
    	}
    	usleep(opcuaUpateTime);
    	//fault mark
    	sprintf(data,"29,put:%d",0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//fault time
    	sprintf(data,"30,put:%d",0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//task list
    	sprintf(data,"31,put:%d",0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//now task
    	sprintf(data,"32,put:%d",get_move_state(0));
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//move pattern
    	sprintf(data,"34,put:%d",1);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//control cycle
    	sprintf(data,"35,put:%d",1000);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//move trajectory
    	sprintf(data,"36,put:%d,%d,%d,%d,%d,%d",0,0,0,0,0,0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//expect joint
    	sprintf(data,"37,put:%f,%f,%f,%f,%f,%f",0,0,0,0,0,0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
        //joint
    	robot_getJoint(joint_, 0);
    	sprintf(data,"38,put:%f,%f,%f,%f,%f,%f",joint_[0],joint_[1],joint_[2],joint_[3],joint_[4],joint_[5]);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//expect velocity
    	sprintf(data,"39,put:%f,%f,%f,%f,%f,%f",0,0,0,0,0,0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
        //joint velocity
        robot_getJoint(joint, 0);
        sprintf(data,"40,put:%f,%f,%f,%f,%f,%f",(joint[0]-joint_[0])/opcuaUpataTime_s,(joint[1]-joint_[1])/opcuaUpataTime_s,(joint[2]-joint_[2])/opcuaUpataTime_s,(joint[3]-joint_[3])/opcuaUpataTime_s,(joint[4]-joint_[4])/opcuaUpataTime_s,(joint[5]-joint_[5])/opcuaUpataTime_s);
        SocketSendString(data, opcuName);
        usleep(opcuaUpateTime);
    	//expect torque
    	sprintf(data,"41,put:%f,%f,%f,%f,%f,%f",0,0,0,0,0,0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//torque
    	sprintf(data,"42,put:%f,%f,%f,%f,%f,%f",0,0,0,0,0,0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//expect pos
        sprintf(data,"43,put:%f,%f,%f",0,0,0);
        SocketSendString(data, opcuName);
        usleep(opcuaUpateTime);
        //pos
        robot_getCartesian(NULL,NULL, pospose, 0);
        sprintf(data,"44,put:%f,%f,%f",pospose[0],pospose[1],pospose[2]);
        SocketSendString(data, opcuName);
        usleep(opcuaUpateTime);
        //expect pose
	sprintf(data,"45,put:%f,%f,%f",0,0,0);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //pose
	robot_getPosAndPose(pospose);
	sprintf(data,"46,put:%f,%f,%f",pospose[3],pospose[4],pospose[5]);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //tase ID
	sprintf(data,"84,put:%d",get_move_state(0));
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //tase path
	sprintf(data,"85,put:%s","/hanbing");
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //tase state
	sprintf(data,"86,put:%d",get_move_state(0));
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //operation order
	sprintf(data,"87,put:%d",get_move_state(0));
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //cyclic sign
	sprintf(data,"88,put:%d",0);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //velocity sign
	sprintf(data,"89,put:%d",1);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //poweroff
	sprintf(data,"91,put:%d",1);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //move pattern
	sprintf(data,"92,put:%d",1);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //control order
	sprintf(data,"93,put:%d",get_move_state(0));
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);

	sprintf(data,"94,put:%d",get_move_state(0));
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
	//execute state
	sprintf(data,"95,put:%d",get_robot_power_state(0));
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
	//error informatization
	sprintf(data,"96,put:%d",0);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
	//operation limits
	sprintf(data,"97,put:%d",1);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
	//operation limits
	SocketSendString("98,get", opcuName);
	memset(data,'\0',sizeof(data));
	SocketRecvString(data, opcuName);
	if (atoi(&(data[strlen(data)-1]))==1)
	{

	}
	//order ID
	sprintf(data,"100,put:%d",get_move_state(0));
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);

    	//control cycle
    	sprintf(data,"101,put:%d",1000);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//interpolation pattern
    	sprintf(data,"102,put:%d",1000);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//expect pos and pose
        sprintf(data,"103,put:%f,%f,%f,%f,%f,%f",0,0,0,0,0,0);
        SocketSendString(data, opcuName);
        usleep(opcuaUpateTime);
    	//expect velocity
    	sprintf(data,"104,put:%d",0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
    	//continuous sign
    	sprintf(data,"105,put:%d",0);
    	SocketSendString(data, opcuName);
    	usleep(opcuaUpateTime);
        //DO
	sprintf(data,"130,put:%d",0);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //AO
	sprintf(data,"132,put:%d",0);
	SocketSendString(data, opcuName);
	usleep(opcuaUpateTime);
        //DI
	SocketSendString("129,get", opcuName);
	memset(data,'\0',sizeof(data));
	SocketRecvString(data, opcuName);
	usleep(opcuaUpateTime);
        //AI
	SocketSendString("131,get", opcuName);
	memset(data,'\0',sizeof(data));
	SocketRecvString(data, opcuName);
	usleep(opcuaUpateTime);


        //power method
	SocketSendString("299,get", opcuName);
	memset(data,'\0',sizeof(data));
	SocketRecvString(data, opcuName);
	d=&(data[strlen(data)-1]);
	powerdata=atoi(d);
	if ((1==powerdata)&&(0==powerflag))
	{
		powerflag=1;
		move_start();
		gettimestart(&time);
		printf("power\n");
	}
	if ((0==powerdata)&&(1==powerflag))
	{
		powerflag=0;
		move_stop();
		printf("power off\n");
	}

	usleep(opcuaUpateTime);
    }


	return 0;
}


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

int dynamics_direct_teach_start(double *StabilGain, double *BoundGain, double friction_die, double *fv_compensation, double *fc_positive_compensation, double *fc_negative_compensation,double angle_constraint, double velocity_constraint, double torque_constraint, uint32_t select_axis,TOOL *rtool, WOBJ *rwobj, int robot_index);

int dynamics_direct_teach_end(int robot_index);

int MainModule()
{

  InputEvent ie;
  int ret=input_event_open(&ie,"/dev/input/event9",0x01,0,0);
  printf("ret=%d\n",ret);
  struct input_event ev;
  SetDo(0,1);
  SetDo(0,0);
  SetDo(16,1);
  SetDo(16,0);


  RobotPoweroff(0);
  RobotPoweroff(1);
  double StabilGain[7]={0.1,0.1,0.1,0.1,0.1,0.1,0.1};
  double BoundGain[7]={500,500,500,500,200,200,200};
  double friction_die=0;
  double fv_compensation[7]={0,0,0,1,1,1,1};
  double fc_positive_compensation[7]={0,0,0,0,0,0,0};
  double fc_negative_compensation[7]={0,0,0,0,0,0,0};
  double angle_constraint=0.8;
  double velocity_constraint=1;
  double torque_constraint=1;
  uint64_t select_axis=0xFF;
  IMPORTTOOL(tooll1);
  IMPORTTOOL(toolr1);
  dynamics_direct_teach_start(StabilGain,BoundGain,friction_die,fv_compensation,fc_positive_compensation,fc_negative_compensation,angle_constraint,velocity_constraint,torque_constraint,select_axis,tooll1,NULL,0);
  dynamics_direct_teach_start(StabilGain,BoundGain,friction_die,fv_compensation,fc_positive_compensation,fc_negative_compensation,angle_constraint,velocity_constraint,torque_constraint,select_axis,toolr1,NULL,1);

  Rsleep(2000);
  const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
  RobotPower(0); 
  RobotPower(1);
  Rsleep(2000);
  RTimer mytimer;
  initUserTimer(&mytimer, 0, 1);
  double target[15]={0,0,0,0,0,0,0,0,0,0};
  const char* robot_namel=get_name_robot_device(get_deviceName(0,NULL),0);
  const char* robot_namer=get_name_robot_device(get_deviceName(0,NULL),1);
  int n=14;
  while (robot_ok())
  {
    userTimer(&mytimer);
    GetGroupPosition(robot_namel,target);
    GetGroupPosition(robot_namer,&(target[7]));
    ret=input_event_read(&ie,&ev);
    if (0==ret)
    {
      target[n]=ev.code;
      printf("==%d\n",ev.code);
      if (2==ev.code)
      {
	      printf("2\n");
	SetDo(1,1);
	SetDo(17,1);
	Rsleep(50);
	SetDo(1,0);
	SetDo(17,0);
      }
      if(3==ev.code)
      {
	      printf("3\n");
      	SetDo(0,1);
	SetDo(16,1);
	Rsleep(50);
	SetDo(0,0);
	SetDo(16,0);
      }
     }
     else
     {
        target[n]=0;
     }
     RSaveDataFast1("drag_data",1, 100, n+1, target );   
  } 

dynamics_direct_teach_end(0);
dynamics_direct_teach_end(1);
	
	
	
	
	return 0;
}

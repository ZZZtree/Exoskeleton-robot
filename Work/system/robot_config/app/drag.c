
#include "HYYRobotInterface.h"
int MainModule()
{
  RobotPoweroff(0);
  double StabilGain[7]={0.1,0.1,0.1,0.1,0.1,0.1,0.1};
  double BoundGain[7]={500,500,500,500,200,200,200};
  double friction_die=0;
  double fv_compensation[7]={0,0,0,1,1,1,1};
  double fc_positive_compensation[7]={0,0,0,0,0,0,0};
  double fc_negative_compensation[7]={0,0,0,0,0,0,0};
  double angle_constraint=0.8;
  double velocity_constraint=0.5;
  double torque_constraint=1;
  uint64_t select_axis=0xFF;
  robot_dynamics_direct_teach_start(StabilGain,BoundGain,friction_die,fv_compensation,fc_positive_compensation,fc_negative_compensation,angle_constraint,velocity_constraint,torque_constraint,select_axis);

  Rsleep(2000);
  const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
  RobotPower(0); 
   while (robot_ok())
   {
     Rsleep(1000000000);
   } 

robot_dynamics_direct_teach_end();
	
	
	
	
	return 0;
}

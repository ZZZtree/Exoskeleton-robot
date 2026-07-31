#include "HYYRobotInterface.h"

int MainModule()
{
	int ret=0;
	ret=CreateGrip2("grip","grip");
	Rsleep(5000);


	ret=ControlGrip("grip", 1);
	Rsleep(5000);


	ret=ControlGrip("grip", 0);
	Rsleep(5000);

	return 0;
	
}

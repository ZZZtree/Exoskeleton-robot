
#include "HYYRobotInterface.h"
#include <math.h>
int MainModule()
{
	robpose p1;
	robpose p1_mid;

	speed sp;
	getspeed("v1",&sp);
	zone zo;
	getzone("z3",&zo);
	zone zo1;
	getzone("z4",&zo1);
	move_start();
	tool to;
	gettool("tool13",&to);
	wobj wo;
	getwobj("wobj13",&wo);
	
	double data[7];
	double _data[50][6];
	while(0!=RReadDataFast1("buaa1",1, 1000, 7, data ))
	{
		printf("read data lost!\n");
		Rsleep(1000);
	}
	memcpy(&p1,data,6*sizeof(double));
	robpose p1_offset=Offs(&p1, 0, 0, 50, 0, 0, 0);
	robpose p1_end=p1;
	printf("moveJ\n");
	moveJ(&p1_offset,&sp,NULL,&to,&wo);	
	
	moveL(&p1,&sp,NULL,&to,&wo);
	int flag=0;
	int count=0;
	while (robot_ok())
	{
		if (0!=RReadDataFast1("buaa1",1, 1000, 7, data))
		{
			printf("read data lost!\n");
			break;
		}
		if (0==data[6])
		{
			flag=0;
			count=0;
			while (robot_ok())
			{
				memcpy(_data[count],data,6*sizeof(double));
				count++;
				if (30==count)
				{
					break;
				}
				if (0!=RReadDataFast1("buaa1",1, 1000, 7, data))
				{
					printf("read data lost!\n");
					break;
				}
				if (data[6]==1)
				{
					memcpy(_data[count],data,6*sizeof(double));
					flag=1;
					break;
				}
			}

		}
		else
		{
			flag=2;
			
		}
		if (0==flag)
		{
			memcpy(&p1,_data[count-1],6*sizeof(double));
			memcpy(&p1_mid,_data[(count-1)/2],6*sizeof(double));
			printf("C\n");
			moveC(&p1,&p1_mid,&sp,&zo1,&to,&wo);
		}
		else if(1==flag)
		{
			memcpy(&p1,_data[count-1],6*sizeof(double));
			memcpy(&p1_mid,_data[(count-1)/2],6*sizeof(double));
			printf("C\n");
			moveC(&p1,&p1_mid,&sp,&zo1,&to,&wo);
			
			memcpy(&p1,_data[count],6*sizeof(double));
			printf("L\n");
			moveL(&p1,&sp,&zo,&to,&wo);
			
		}
		else if(2==flag)
		{
			memcpy(&p1,data,6*sizeof(double));
			printf("L\n");
			moveL(&p1,&sp,&zo,&to,&wo);
		}
			
	}
	printf("moveL\n");
	moveL(&p1_end,&sp,NULL,&to,&wo);	

	sleep(2);
	move_stop();

    return 0;
}


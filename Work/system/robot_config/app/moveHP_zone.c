
#include "HYYRobotInterface.h"
#include <math.h>
extern void tr2rpy(double R[3][3], double* rpy, int flag, int solve);
int MainModule()
{
	robjoint j0;
	getrobjoint("j10", &j0);
	robpose p1;
	getrobpose("p11",&p1);
	robpose p2;
	getrobpose("p12",&p2);
	speed sp;
	getspeed("v1",&sp);
	zone zo;
	getzone("z3",&zo);
	move_start();
	
	double t=0;
	double r=100;
	double O[3]={200,0,305};
	double a=sqrt(2)/2.0;
	double x=r*a*cos(t)+O[0];
	double y=r*sin(t)+O[1];
	double z=r*a*cos(t)+O[2];
	double R[3][3];
	R[0][0]=a*sin(t);R[0][1]=-a;R[0][2]=-a*cos(t);
	R[1][0]=-cos(t);R[1][1]=0;R[1][2]=-sin(t);
	R[2][0]=a*sin(t);R[2][1]=a;R[2][2]=-a*cos(t);
	tr2rpy(R, p1.kps, 2, 0);
	p1.xyz[0]=x;
	p1.xyz[1]=y;
	p1.xyz[2]=z;
	printf("moveA\n");
	moveJ(&p1,&sp,NULL,NULL,NULL);	

printf("11111111111\n");
	p1.xyz[0]=x+100;
	p1.xyz[1]=y;
	p1.xyz[2]=z;
	int ret=moveHP(&p1, 20, 10, &sp, &zo, NULL, NULL);
printf("%d\n",ret);
	p1.xyz[0]=x;
	p1.xyz[1]=y+100;
	p1.xyz[2]=z;
	moveHP(&p1, 10, 20, &sp, &zo, NULL, NULL);

	p1.xyz[0]=x;
	p1.xyz[1]=y;
	p1.xyz[2]=z+50;
	moveL(&p1, &sp, NULL, NULL, NULL);



	sleep(2);
	move_stop();

    return 0;
}

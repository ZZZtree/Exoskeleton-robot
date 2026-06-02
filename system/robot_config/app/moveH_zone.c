
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
	getrobpose("p11",&p2);
	robpose p3;
	getrobpose("p11",&p3);
	speed sp;
	getspeed("v1",&sp);
	zone zo;
	getzone("z3",&zo);
	move_start();
	
	double t=0;
	double r=10;
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
	tr2rpy(R, p2.kps, 2, 0);
	tr2rpy(R, p3.kps, 2, 0);
	p1.xyz[0]=x;
	p1.xyz[1]=y;
	p1.xyz[2]=z;
	printf("moveA\n");
	int ret=moveJ(&p1,&sp,NULL,NULL,NULL);	
printf("%d\n",ret);

	p1.xyz[0]=x+100;
	p1.xyz[1]=y;
	p1.xyz[2]=z;

	t=0.2;
	R[0][0]=a*sin(t);R[0][1]=-a;R[0][2]=-a*cos(t);
	R[1][0]=-cos(t);R[1][1]=0;R[1][2]=-sin(t);
	R[2][0]=a*sin(t);R[2][1]=a;R[2][2]=-a*cos(t);
	tr2rpy(R, p1.kps, 2, 0);

	t=0.1;
	p3.xyz[0]=r*a*cos(t)+O[0];
	p3.xyz[1]=r*sin(t)+O[1];
	p3.xyz[2]=r*a*cos(t)+O[2];

	t=0.5;
	p2.xyz[0]=r*a*cos(t)+O[0];
	p2.xyz[1]=r*sin(t)+O[1];
	p2.xyz[2]=r*a*cos(t)+O[2];



	ret=moveH(&p2, &p3, &p1, 10,&sp, &zo, NULL, NULL);

printf("%d\n",ret);
	p1.xyz[0]=x+100;
	p1.xyz[1]=y+100;
	p1.xyz[2]=z;

	t=0.5;
	R[0][0]=a*sin(t);R[0][1]=-a;R[0][2]=-a*cos(t);
	R[1][0]=-cos(t);R[1][1]=0;R[1][2]=-sin(t);
	R[2][0]=a*sin(t);R[2][1]=a;R[2][2]=-a*cos(t);
	tr2rpy(R, p1.kps, 2, 0);

	p3.xyz[0]=x+105;
	p3.xyz[1]=y;
	p3.xyz[2]=z+5;

	p2.xyz[0]=x+100;
	p2.xyz[1]=y;
	p2.xyz[2]=z+10;



	ret=moveH(&p2, &p3, &p1,10, &sp, &zo, NULL, NULL);
printf("%d\n",ret);
	p1.xyz[0]=x;
	p1.xyz[1]=y;
	p1.xyz[2]=z+50;


	t=0.6;
	R[0][0]=a*sin(t);R[0][1]=-a;R[0][2]=-a*cos(t);
	R[1][0]=-cos(t);R[1][1]=0;R[1][2]=-sin(t);
	R[2][0]=a*sin(t);R[2][1]=a;R[2][2]=-a*cos(t);
	tr2rpy(R, p1.kps, 2, 0);
	ret=moveL(&p1, &sp, NULL, NULL, NULL);
printf("%d\n",ret);


	sleep(2);
	move_stop();

    return 0;
}

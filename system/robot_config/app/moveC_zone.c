
#include "HYYRobotInterface.h"
#include <math.h>
extern void tr2rpy(double R[3][3], double* rpy, int flag, int solve);
int MainModule()
{
	robjoint j0;
	getrobjoint("j10", &j0);
	robpose p1;
	//getrobpose("p11",&p1);
	robpose p2;
	//getrobpose("p12",&p2);
	speed sp;
	getspeed("v500",&sp);
	zone zo;
	getzone("z3",&zo);
	move_start();
	
	double t=0;
	double r=0.050;
	double O[3]={0.350,0,0.385};
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
	while (t<17)
	{
		t=t+0.05;
		p2.xyz[0]=r*a*cos(t)+O[0];
		p2.xyz[1]=r*sin(t)+O[1];
		p2.xyz[2]=r*a*cos(t)+O[2];
		t=t+0.1;
		p1.xyz[0]=r*a*cos(t)+O[0];
		p1.xyz[1]=r*sin(t)+O[1];
		p1.xyz[2]=r*a*cos(t)+O[2];

		R[0][0]=a*sin(t);R[0][1]=-a;R[0][2]=-a*cos(t);
		R[1][0]=-cos(t);R[1][1]=0;R[1][2]=-sin(t);
		R[2][0]=a*sin(t);R[2][1]=a;R[2][2]=-a*cos(t);
//		tr2rpy(R, p1.kps, 2, 0);


		//printf("%f,%f,%f,%f,%f,%f\n",p1.xyz[0],p1.xyz[1],p1.xyz[2],p1.kps[0],p1.kps[1],p1.kps[2]);
		moveC(&p1,&p2,&sp,&zo,NULL,NULL);	
	}
	printf("moveC\n");

	t=t+0.05;
	p2.xyz[0]=r*a*cos(t)+O[0];
	p2.xyz[1]=r*sin(t)+O[1];
	p2.xyz[2]=r*a*cos(t)+O[2];
	t=t+5;
	p1.xyz[0]=r*a*cos(t)+O[0];
	p1.xyz[1]=r*sin(t)+O[1];
	p1.xyz[2]=r*a*cos(t)+O[2];
	moveC(&p1,&p2,&sp,NULL,NULL,NULL);


	sleep(2);
	move_stop();

    return 0;
}

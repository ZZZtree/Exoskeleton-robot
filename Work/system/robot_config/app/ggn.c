
#include "HYYRobotInterface.h"

robpose* mid_ropose(robpose* c,robpose* m,robpose* ret, double offset)
{
	if (c->xyz[0]>m->xyz[0])
	{
		ret->xyz[0]=c->xyz[0]-offset;
	}
	else
	{
		ret->xyz[0]=c->xyz[0]+offset;
	}
	if (c->xyz[1]>m->xyz[1])
	{
		ret->xyz[1]=c->xyz[1]-offset;
	}
	else
	{
		ret->xyz[1]=c->xyz[1]+offset;
	}
	ret->xyz[2]=c->xyz[2];
	ret->kps[0]=c->kps[0];
	ret->kps[1]=c->kps[1];
	ret->kps[2]=c->kps[2];
	return ret;
}

int MainModule()
{
	robpose mid;
	double un=0.001;
	double z=0.10;
	double o=0.0176776695296637;
	double k=3.14159265358;
	double p=0;
	double s=0;

	double x=-0.005;
	SETPOSE(j0,x,-5*un,4*z,k,p,s);
	SETPOSE(j1,x,-5*un,z,k,p,s);
	SETPOSE(j2,x,33*un,z,k,p,s);
	SETPOSE(j3,x,71*un,z,k,p,s);
	SETPOSE(j4,x,109*un,z,k,p,s);
	SETPOSE(j5,x,147*un,z,k,p,s);



	x=33*un;
	SETPOSE(j6,x,-5*un,z,k,p,s);
	SETPOSE(j7,x,33*un,z,k,p,s);
	SETPOSE(j8,x,71*un,z,k,p,s);
	SETPOSE(j9,x,109*un,z,k,p,s);
	SETPOSE(j10,x,147*un,z,k,p,s);

	x=71*un;
	SETPOSE(j11,x,-5*un,z,k,p,s);
	SETPOSE(j12,x,33*un,z,k,p,s);
	SETPOSE(j13,x,71*un,z,k,p,s);
	SETPOSE(j14,x,109*un,z,k,p,s);
	SETPOSE(j15,x,147*un,z,k,p,s);

	x=109*un;
	SETPOSE(j16,x,-5*un,z,k,p,s);
	SETPOSE(j17,x,33*un,z,k,p,s);
	SETPOSE(j18,x,71*un,z,k,p,s);
	SETPOSE(j19,x,109*un,z,k,p,s);
	SETPOSE(j20,x,147*un,z,k,p,s);

	x=147*un;
	SETPOSE(j21,x,-5*un,z,k,p,s);
	SETPOSE(j22,x,33*un,z,k,p,s);
	SETPOSE(j23,x,71*un,z,k,p,s);
	SETPOSE(j24,x,109*un,z,k,p,s);
	SETPOSE(j25,x,147*un,z,k,p,s);

	x=185*un;
	SETPOSE(j26,x,-5*un,z,k,p,s);
	SETPOSE(j27,x,33*un,z,k,p,s);
	SETPOSE(j28,x,71*un,z,k,p,s);
	SETPOSE(j29,x,109*un,z,k,p,s);
	SETPOSE(j30,x,147*un,z,k,p,s);

	x=223*un;
	SETPOSE(j31,x,-5*un,z,k,p,s);
	SETPOSE(j32,x,33*un,z,k,p,s);
	SETPOSE(j33,x,71*un,z,k,p,s);
	SETPOSE(j34,x,109*un,z,k,p,s);
	SETPOSE(j35,x,147*un,z,k,p,s);

	SETSPEED(sp,0.05,0.50);
	SETSPEED(sp1,0.05,0.3);//0.3
	SETSPEED(sp2,0.05,0.5);//0.5
	SETZONESIZE(zone,0.01)
	IMPORTTOOL(toolgg);
	IMPORTWOBJ(wobjgg);


	MoveJ(j0,sp,NULL,toolgg,wobjgg);

	MoveL(j1,sp,NULL,toolgg,wobjgg);
	int i=0;
	for (i=0;i<3;i++)
	{


	MoveL(j4,sp,zone,toolgg,wobjgg);
	MoveC(j14,j10,sp,zone,toolgg,wobjgg);
	MoveL(j12,sp,zone,toolgg,wobjgg);
	MoveC(j22,j16,sp,zone,toolgg,wobjgg);
	MoveL(j24,sp,zone,toolgg,wobjgg);
	MoveC(j34,j30,sp,zone,toolgg,wobjgg);
	MoveL(j32,sp,zone,toolgg,wobjgg);
	MoveC(j22,j26,sp,zone,toolgg,wobjgg);
	MoveL(j24,sp,zone,toolgg,wobjgg);
	MoveC(j14,j20,sp,zone,toolgg,wobjgg);
	MoveL(j12,sp,zone,toolgg,wobjgg);
	MoveC(j8,mid_ropose(j2,j6,&mid, o),sp,zone,toolgg,wobjgg);
	MoveL(j28,sp,zone,toolgg,wobjgg);
	MoveC(j22,mid_ropose(j32,j26,&mid, o),sp,zone,toolgg,wobjgg);
	MoveC(j12,j18,sp,zone,toolgg,wobjgg);
	MoveC(j8,mid_ropose(j6,j2,&mid, o),sp,zone,toolgg,wobjgg);
	MoveC(j14,mid_ropose(j8,j14,&mid, o),sp,zone,toolgg,wobjgg);
	MoveC(j24,j20,sp,zone,toolgg,wobjgg);
	MoveC(j30,mid_ropose(j28,j34,&mid, o),sp,zone,toolgg,wobjgg);
	MoveC(j28,j24,sp,zone,toolgg,wobjgg);
	MoveC(j26,j32,sp,zone,toolgg,wobjgg);
	MoveL(j1,sp1,NULL,toolgg,wobjgg);
	}
	Rsleep(2000);

	return 0;
}

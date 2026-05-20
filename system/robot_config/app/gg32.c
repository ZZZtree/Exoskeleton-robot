
#include "HYYRobotInterface.h"
#include "math.h"
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
		ret->xyz[1]=m->xyz[1]+offset;
	}
	else
	{
		ret->xyz[1]=m->xyz[1]-offset;
	}
	ret->xyz[2]=c->xyz[2];
	ret->kps[0]=c->kps[0];
	ret->kps[1]=c->kps[1];
	ret->kps[2]=c->kps[2];
	return ret;
}
#define R_PI 3.141592653
void rpy2r(double* rpy, double R[3][3])
{
	double sx = sin(rpy[0]);
	double cx = cos(rpy[0]);
	double sy = sin(rpy[1]);
	double cy = cos(rpy[1]);
	double sz = sin(rpy[2]);
	double cz = cos(rpy[2]);
	//R=Rz*Ry*Rx
	R[0][0] = cy*cz; R[0][1] = cz*sx*sy - cx*sz; R[0][2] = sx*sz + cx*cz*sy;
	R[1][0] = cy*sz; R[1][1] = cx*cz + sx*sy*sz; R[1][2] = cx*sy*sz - cz*sx;
	R[2][0] = -sy; R[2][1] = cy*sx; R[2][2] = cx*cy;
}

void r2rpy(double R[3][3],double* rpy)
{
	rpy[1] = atan2(-R[2][0],sqrt(R[0][0]*R[0][0]+R[1][0]*R[1][0])); //pitch
	if (fabs(fabs(rpy[1]) - R_PI / 2.0) < 1e-6)
	{
		if (rpy[1] > 0) //pi/2
		{
			rpy[1] = R_PI / 2.0;
			rpy[2] = 0.0;
			rpy[0] = atan2(R[0][1],R[1][1]);
		}
		else//-pi/2
		{
			rpy[1] = -R_PI / 2.0;
			rpy[2] = 0.0;
			rpy[0] = -atan2(R[0][1], R[1][1]);
		}
	}
	else
	{
		double cp = cos(rpy[1]);
		rpy[2] = atan2(R[1][0]/cp,R[0][0]/cp);
		rpy[0] = atan2(R[2][1]/cp, R[2][2]/cp);
	}
}

void rxr(double(*R1)[3], double(*R2)[3], double(*Rres)[3])
{
	Rres[0][0] = R1[0][0] * R2[0][0] + R1[0][1] * R2[1][0] + R1[0][2] * R2[2][0];
	Rres[0][1] = R1[0][0] * R2[0][1] + R1[0][1] * R2[1][1] + R1[0][2] * R2[2][1];
	Rres[0][2] = R1[0][0] * R2[0][2] + R1[0][1] * R2[1][2] + R1[0][2] * R2[2][2];

	Rres[1][0] = R1[1][0] * R2[0][0] + R1[1][1] * R2[1][0] + R1[1][2] * R2[2][0];
	Rres[1][1] = R1[1][0] * R2[0][1] + R1[1][1] * R2[1][1] + R1[1][2] * R2[2][1];
	Rres[1][2] = R1[1][0] * R2[0][2] + R1[1][1] * R2[1][2] + R1[1][2] * R2[2][2];

	Rres[2][0] = R1[2][0] * R2[0][0] + R1[2][1] * R2[1][0] + R1[2][2] * R2[2][0];
	Rres[2][1] = R1[2][0] * R2[0][1] + R1[2][1] * R2[1][1] + R1[2][2] * R2[2][1];
	Rres[2][2] = R1[2][0] * R2[0][2] + R1[2][1] * R2[1][2] + R1[2][2] * R2[2][2];
}
void* robot_corr(void* arg)
{
	int robot_index=*((int*)arg);

	IMPORTWOBJ(wobjgg12);
	IMPORTWOBJ(wobjgg2);

	//tool* to=NULL;
	wobj* wo=NULL;
	if (0==robot_index)
	{
		//to=toolgg1;
		wo=wobjgg12;
	}
	if (1==robot_index)
	{
		//to=toolgg2;
		wo=wobjgg2;
	}


	RTimer mytimer;
	initUserTimer(&mytimer, robot_index, 1);
	robpose pc;
	GetCurrentTargetCartesian (NULL, wo, &pc, robot_index+1);
	double R[3][3];
        rpy2r(pc.kps, R);

	//inverse
	double P[6];
	int i=0;
	for (i=0;i<3;i++)
	{

		P[i]=-(R[0][i]*pc.xyz[0]+R[1][i]*pc.xyz[1]+R[2][i]*pc.xyz[2]);
	}
	double c;
	c = R[0][1]; R[0][1] = R[1][0]; R[1][0] = c;
	c = R[0][2]; R[0][2] = R[2][0]; R[2][0] = c;
	c = R[1][2]; R[1][2] = R[2][1]; R[2][1] = c;
	r2rpy(R,&(P[3]));
	SETTOOL_FRAME(to,1,P[0],P[1],P[2],P[3],P[4],P[5]);

	memset(&pc,0,sizeof(robpose));
	robpose pc_1=pc;
	double target[6]={0,0,0,0,0,0};

	while (robot_ok())
	{
		userTimer(&mytimer);
		usleep(300);
		while (robot_ok())
		{
			GetCurrentTargetCartesian (to, wo, &pc, robot_index+1);
			if (0!=memcmp(&pc,&pc_1,sizeof(robpose)))
			{
				pc_1=pc;
				break;
			}
			usleep(100);
		}
		
		target[0]=pc.xyz[0];target[1]=pc.xyz[1];target[2]=pc.xyz[2];
		target[3]=pc.kps[0];target[4]=pc.kps[1];target[5]=pc.kps[2];
		//printf("%f,%f,%f,%f,%f,%f\n",target[0],target[1],target[2],target[3],target[4],target[5]);
		SetEGMInput(target,robot_index);
	}
	return NULL;
}


void* robot_move(void* arg)
{
	int robot_index=*((int*)arg);
	IMPORTTOOL(toolgg0);
	IMPORTWOBJ(wobjgg0);
	IMPORTTOOL(toolgg1);
	IMPORTWOBJ(wobjgg1);
	robpose mid;
	double un=0.001;
	double z=0.084;
	double o=0.038/sqrt(2);
	double k=0;
	double p=0;
	double s=0;

	if (1==robot_index)
	{
		k=-3.116416;
		p=0.030314;
		s=1.000184;
	}

	double x=-0.005;
	SETPOSE(j0,x,-5*un,z+0.03,k,p,s);
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

	SETSPEED(sp,0.05,0.3);
	SETSPEED(sp1,0.05,0.3);//0.3
	SETSPEED(sp2,0.05,0.3);//0.5
	SETZONESIZE(zone,0.01);

	
	tool* to=NULL;
	wobj* wo=NULL;
	if (0==robot_index)
	{
		to=toolgg0;
		wo=wobjgg0;
	}
	if (1==robot_index)
	{
		to=toolgg1;
		wo=wobjgg1;
	}
	//printf("%f,%f,%f,%f,%f,%f\n",j0->xyz[0],j0->xyz[1],j0->xyz[2],j0->kps[0],j0->kps[1],j0->kps[2]);
	//printf("%f,%f,%f,%f,%f,%f\n",to->tframe.xyz[0],to->tframe.xyz[1],to->tframe.xyz[2],to->tframe.kps[0],to->tframe.kps[1],to->tframe.kps[2]);
	//printf("%f,%f,%f,%f,%f,%f\n",wo->oframe.xyz[0],wo->oframe.xyz[1],wo->oframe.xyz[2],wo->oframe.kps[0],wo->oframe.kps[1],wo->oframe.kps[2]);
	Rsleep(1000);
	MultiMoveL(j0,sp,NULL,to,wo,robot_index);
        MultiMoveL(j1,sp,NULL,to,wo,robot_index);


	int i=0;
	for (i=0;i<1;i++)
	{
		MultiMoveL(j4,sp,zone,to,wo,robot_index);
		MultiMoveC(j14,j10,sp,zone,to,wo,robot_index);
		MultiMoveL(j12,sp,zone,to,wo,robot_index);
		MultiMoveC(j22,j16,sp,zone,to,wo,robot_index);
		MultiMoveL(j24,sp,zone,to,wo,robot_index);
		MultiMoveC(j34,j30,sp,zone,to,wo,robot_index);
		MultiMoveL(j32,sp,zone,to,wo,robot_index);
		MultiMoveC(j22,j26,sp,zone,to,wo,robot_index);
		MultiMoveL(j24,sp,zone,to,wo,robot_index);
		MultiMoveC(j14,j20,sp,zone,to,wo,robot_index);
		MultiMoveL(j12,sp,zone,to,wo,robot_index);
		MultiMoveC(j8,j6,sp,zone,to,wo,robot_index);
		MultiMoveL(j28,sp,zone,to,wo,robot_index);
		MultiMoveC(j22,j32,sp,zone,to,wo,robot_index);
		MultiMoveC(j12,j18,sp,zone,to,wo,robot_index);
		MultiMoveC(j8,j6,sp,zone,to,wo,robot_index);
		MultiMoveC(j14,mid_ropose(j8,j14,&mid, o),sp,zone,to,wo,robot_index);
		MultiMoveC(j24,j20,sp,zone,to,wo,robot_index);
		MultiMoveC(j30,j28,sp,zone,to,wo,robot_index);
		MultiMoveC(j28,j24,sp,zone,to,wo,robot_index);
		MultiMoveC(j26,j32,sp,zone,to,wo,robot_index);
		MultiMoveL(j1,sp1,NULL,to,wo,robot_index);
	}

	return NULL;
}

int MainModule()
{


	SETSPEED(sp,0.05,0.05);
SETSPEED(sp1,0.05,0.05);
	IMPORTTOOL(toolgg0);
	IMPORTWOBJ(wobjgg0);

	IMPORTTOOL(toolgg1);
	IMPORTWOBJ(wobjgg1);

	//IMPORTTOOL(toolgg2);
	//IMPORTWOBJ(wobjgg2);

	SETZONESIZE(zone,0.01);



        IMPORTJOINT(ji21);
        MultiMoveA(ji21,sp,NULL3,2);

        IMPORTJOINT(ji0);
        MultiMoveA(ji0,sp,NULL3,0);
        IMPORTJOINT(ji1);
        MultiMoveA(ji1,sp,NULL3,1);
        IMPORTJOINT(ji2);
        MultiMoveA(ji2,sp,NULL3,2);
//Rsleep(100000000);
	int robot0=0;
        int robot1=1;
	int ret=0;
	ret=EGMCreate(2,toolgg0, wobjgg0,0);
	if (0!=ret)
	{
		printf("robot 0 EGMCreate failure\n");
		return 0;
	}
	ret=EGMStart(1, 0);
	if (0!=ret)
	{
		printf("robot 0 EGMStart failure\n");
		return 0;
	}

	ret=EGMCreate(2,toolgg1, wobjgg1,1);
	if (0!=ret)
	{
		printf("robot 1 EGMCreate failure\n");
		return 0;
	}
	ret=EGMStart(1, 1);
	if (0!=ret)
	{
		printf("robot 1 EGMStart failure\n");
		return 0;
	}
	Rsleep(500);

	ret=ThreadCreat(robot_corr,&robot0,"robot0_corr",1 );
	if (0!=ret)
	{
		printf("robot 0 ThreadCreat failure\n");
		return 0;
	}
	Rsleep(500);
	ret=ThreadCreat(robot_corr,&robot1,"robot1_corr",1 );
	if (0!=ret)
	{
		printf("robot 1 ThreadCreat failure\n");
		return 0;
	}
	Rsleep(500);
	ret=ThreadCreat(robot_move,&robot0,"robot0_move",1 );
	if (0!=ret)
	{
		printf("robot 0 ThreadCreat failure\n");
		return 0;
	}
	Rsleep(2000);
	ret=ThreadCreat(robot_move,&robot1,"robot1_move",1 );
	if (0!=ret)
	{
		printf("robot 1 ThreadCreat failure\n");
		return 0;
	}


	IMPORTTOOL(toolgg2);
	IMPORTWOBJ(wobjgg2);
	IMPORTPOSE(p21);
	IMPORTPOSE(p20);
	int i=0;
	for (i=0;i<10;i++)
	{
	      MultiMoveL(p21,sp1,NULL,toolgg2,wobjgg2,2);
              MultiMoveL(p20,sp1,NULL,toolgg2,wobjgg2,2);
	}
	Rsleep(100000000);
	
	ThreadDataFree("robot1_move");
	ThreadDataFree("robot0_move");

	ThreadDataFree("robot1_corr");
	ThreadDataFree("robot0_corr");

	Rsleep(1000);
	EGMStop(1);
	EGMDelete(1);
	EGMStop(0);
	EGMDelete(0);
	Rsleep(1000);
	return 0;
}






#include "HYYRobotInterface.h"
#include "math.h"
static int stop_flag=0;
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

void R_Vec(double(*R)[3], double* v, double * vres)
{
	vres[0] = R[0][0] * v[0] + R[0][1] * v[1] + R[0][2] * v[2];
	vres[1] = R[1][0] * v[0] + R[1][1] * v[1] + R[1][2] * v[2];
	vres[2] = R[2][0] * v[0] + R[2][1] * v[1] + R[2][2] * v[2];
}

void* corr_fun(void* arg)
{
	IMPORTTOOL(toolg2);
	double target[6]={0,0,0,0,0,0};
	RTimer mytimer;
	initUserTimer(&mytimer, 0, 1);
	robpose pc;
	GetCurrentTargetCartesian (toolg2, NULL, &pc, 1);
	robpose pc_1=pc;
	robpose pinit2=pc;
	double R[3][3];
	rpy2r(pinit2.kps, R);
	double Rt[3][3]={{R[0][0],R[1][0],R[2][0]},
                         {R[0][1],R[1][1],R[2][1]},
                         {R[0][2],R[1][2],R[2][2]}};	
	double put_target[6];
	while (robot_ok())
	{
		userTimer(&mytimer);
		usleep(300);
		while (robot_ok())
		{
			GetCurrentTargetCartesian (toolg2, NULL, &pc, 1);
			if (0!=memcmp(&pc,&pc_1,sizeof(robpose)))
			{
				pc_1=pc;
				break;
			}
			usleep(100);
		}
		
		target[0]=pc.xyz[0]-pinit2.xyz[0];target[1]=pc.xyz[1]-pinit2.xyz[1];target[2]=pc.xyz[2]-pinit2.xyz[2];
		target[3]=0;target[4]=0;target[5]=0;
		R_Vec(Rt, target, put_target);
		//printf("%f,%f,%f\n",target[0],target[1],target[2]);
		SetEGMInput(put_target,0);
		//printf("%f,%f,%f\n",target[0],target[1],target[2]);
	}
	return NULL;
	
}


void* robot2_move(void* arg)
{
	IMPORTTOOL(toolg2);
	int robot2_index=1;
	SETSPEED(sp,0.05,0.1);
	SETZONESIZE(zone,0.03);
	robpose ps;
	GetCurrentTargetCartesian (toolg2, NULL, &ps, robot2_index);
	double off=0.08;
	double offhigh=0.03;
	robpose p1=OffsRel(&ps, off, off, offhigh, 0, 0, 0);
	robpose p2=OffsRel(&ps, off, -1*off, -1*offhigh, 0, 0, 0);
	while(robot_ok())
	{
		MultiMoveL(&p1, sp, zone,toolg2,NULL, robot2_index);
		MultiMoveL(&p2, sp, zone,toolg2,NULL, robot2_index);
		MultiMoveL(&ps, sp, NULL,toolg2,NULL, robot2_index);
		if (1==stop_flag)
		{
			break;
		}
	}

	return NULL;
}

int MainModule()
{
	robpose mid;
	double un=0.001;
	double z=0.010;
	double y=-0.005;
	double o=0.0176776695296637;
	double k=3.14159265358;
	double p=0;
	double s=0;
	SETPOSE(j0,-5*un,y,4*z,k,p,s);
	SETPOSE(j1,-5*un,y,z,k,p,s);
	SETPOSE(j2,20*un,y,z,k,p,s);
	SETPOSE(j3,45*un,y,z,k,p,s);
	SETPOSE(j4,70*un,y,z,k,p,s);
	SETPOSE(j5,95*un,y,z,k,p,s);
	SETPOSE(j6,120*un,y,z,k,p,s);
	SETPOSE(j7,145*un,y,z,k,p,s);
	SETPOSE(j8,170*un,y,z,k,p,s);
	SETPOSE(j9,195*un,y,z,k,p,s);
	
	y=20*un;
	SETPOSE(j10,-5*un,y,z,k,p,s);
	SETPOSE(j11,20*un,y,z,k,p,s);
	SETPOSE(j12,45*un,y,z,k,p,s);
	SETPOSE(j13,70*un,y,z,k,p,s);
	SETPOSE(j14,95*un,y,z,k,p,s);
	SETPOSE(j15,120*un,y,z,k,p,s);
	SETPOSE(j16,145*un,y,z,k,p,s);
	SETPOSE(j17,170*un,y,z,k,p,s);
	SETPOSE(j18,195*un,y,z,k,p,s);

	y=45*un;
	SETPOSE(j19,-5*un,y,z,k,p,s);
	SETPOSE(j20,20*un,y,z,k,p,s);
	SETPOSE(j21,45*un,y,z,k,p,s);
	SETPOSE(j22,70*un,y,z,k,p,s);
	SETPOSE(j23,95*un,y,z,k,p,s);
	SETPOSE(j24,120*un,y,z,k,p,s);
	SETPOSE(j25,145*un,y,z,k,p,s);
	SETPOSE(j26,170*un,y,z,k,p,s);
	SETPOSE(j27,195*un,y,z,k,p,s);

	y=70*un;
	SETPOSE(j28,-5*un,y,z,k,p,s);
	SETPOSE(j29,20*un,y,z,k,p,s);
	SETPOSE(j30,45*un,y,z,k,p,s);
	SETPOSE(j31,70*un,y,z,k,p,s);
	SETPOSE(j32,95*un,y,z,k,p,s);
	SETPOSE(j33,120*un,y,z,k,p,s);
	SETPOSE(j34,145*un,y,z,k,p,s);
	SETPOSE(j35,170*un,y,z,k,p,s);
	SETPOSE(j36,195*un,y,z,k,p,s);

	y=95*un;
	SETPOSE(j37,-5*un,y,z,k,p,s);
	SETPOSE(j38,20*un,y,z,k,p,s);
	SETPOSE(j39,45*un,y,z,k,p,s);
	SETPOSE(j40,70*un,y,z,k,p,s);
	SETPOSE(j41,95*un,y,z,k,p,s);
	SETPOSE(j42,120*un,y,z,k,p,s);
	SETPOSE(j43,145*un,y,z,k,p,s);
	SETPOSE(j44,170*un,y,z,k,p,s);
	SETPOSE(j45,195*un,y,z,k,p,s);

	y=120*un;
	SETPOSE(j46,-5*un,y,z,k,p,s);
	SETPOSE(j47,20*un,y,z,k,p,s);
	SETPOSE(j48,45*un,y,z,k,p,s);
	SETPOSE(j49,70*un,y,z,k,p,s);
	SETPOSE(j50,95*un,y,z,k,p,s);
	SETPOSE(j51,120*un,y,z,k,p,s);
	SETPOSE(j52,145*un,y,z,k,p,s);
	SETPOSE(j53,170*un,y,z,k,p,s);
	SETPOSE(j54,195*un,y,z,k,p,s);

	y=145*un;
	SETPOSE(j55,-5*un,y,z,k,p,s);
	SETPOSE(j56,20*un,y,z,k,p,s);
	SETPOSE(j57,45*un,y,z,k,p,s);
	SETPOSE(j58,70*un,y,z,k,p,s);
	SETPOSE(j59,95*un,y,z,k,p,s);
	SETPOSE(j60,120*un,y,z,k,p,s);
	SETPOSE(j61,145*un,y,z,k,p,s);
	SETPOSE(j62,170*un,y,z,k,p,s);
	SETPOSE(j63,195*un,y,z,k,p,s);

	y=170*un;
	SETPOSE(j64,-5*un,y,z,k,p,s);
	SETPOSE(j65,20*un,y,z,k,p,s);
	SETPOSE(j66,45*un,y,z,k,p,s);
	SETPOSE(j67,70*un,y,z,k,p,s);
	SETPOSE(j68,95*un,y,z,k,p,s);
	SETPOSE(j69,120*un,y,z,k,p,s);
	SETPOSE(j70,145*un,y,z,k,p,s);
	SETPOSE(j71,170*un,y,z,k,p,s);
	SETPOSE(j72,195*un,y,z,k,p,s);

	y=195*un;
	SETPOSE(j73,-5*un,y,z,k,p,s);
	SETPOSE(j74,20*un,y,z,k,p,s);
	SETPOSE(j75,45*un,y,z,k,p,s);
	SETPOSE(j76,70*un,y,z,k,p,s);
	SETPOSE(j77,95*un,y,z,k,p,s);
	SETPOSE(j78,120*un,y,z,k,p,s);
	SETPOSE(j79,145*un,y,z,k,p,s);
	SETPOSE(j80,170*un,y,z,k,p,s);
	SETPOSE(j81,195*un,y,z,k,p,s);

	SETSPEED(sp,0.05,0.3);
	SETSPEED(sp1,0.05,0.3);//0.3
	SETSPEED(sp2,0.05,0.3);//0.5
	SETZONESIZE(zone,0.01)
	IMPORTTOOL(toolgg);
	IMPORTWOBJ(wobjgg);

        IMPORTJOINT(jinit22);
        MultiMoveA(jinit22, sp, NULL3, 1);


	MoveJ(j0,sp,NULL,toolgg,wobjgg);
	
	MoveL(j1,sp,NULL,toolgg,wobjgg);
	int ret=EGMCreate(2,toolgg, wobjgg,0);
	printf("EGMCreate:%d\n",ret);
	ret=EGMStart(1, 0);
	printf("EGMStart:%d\n",ret);
	Rsleep(1000);

	ret=ThreadCreat(corr_fun,NULL,"corr",1 );
	Rsleep(1000);
	ret=ThreadCreat(robot2_move,NULL,"robot2move",1 );

//Rsleep(100000000000);
	int i=0;
	for (i=0;i<1;i++)
	{
		MoveL(j8,sp,zone,toolgg,wobjgg);
		MoveC(j26,j18,sp,zone,toolgg,wobjgg);
		MoveL(j20,sp,zone,toolgg,wobjgg);
		MoveC(j38,j28,sp,zone,toolgg,wobjgg);
		MoveL(j44,sp,zone,toolgg,wobjgg);
		MoveC(j62,j54,sp,zone,toolgg,wobjgg);
		MoveL(j56,sp,zone,toolgg,wobjgg);
		MoveC(j74,j64,sp,zone,toolgg,wobjgg);
		MoveL(j80,sp,zone,toolgg,wobjgg);
		MoveC(j72,mid_ropose(j71,j81,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j18,sp,zone,toolgg,wobjgg);
		MoveC(j16,j8,sp,zone,toolgg,wobjgg);
		MoveL(j70,sp,zone,toolgg,wobjgg);
		MoveC(j68,j78,sp,zone,toolgg,wobjgg);
		MoveL(j14,sp,zone,toolgg,wobjgg);
		MoveC(j12,j4,sp,zone,toolgg,wobjgg);
		MoveL(j66,sp,zone,toolgg,wobjgg);
		MoveC(j64,j74,sp,zone,toolgg,wobjgg);
		MoveL(j10,sp,zone,toolgg,wobjgg);

		MoveC(j12,j2,sp,zone,toolgg,wobjgg);
		MoveC(j14,j22,sp,zone,toolgg,wobjgg);
		MoveC(j16,j6,sp,zone,toolgg,wobjgg);
		MoveC(j18,j26,sp,zone,toolgg,wobjgg);
		MoveC(j26,j8,sp,zone,toolgg,wobjgg);
		MoveC(j34,j44,sp,zone,toolgg,wobjgg);
		MoveC(j32,j24,sp,zone,toolgg,wobjgg);
		MoveC(j30,j40,sp,zone,toolgg,wobjgg);
		MoveC(j38,j20,sp,zone,toolgg,wobjgg);
		MoveC(j46,j56,sp,zone,toolgg,wobjgg);
		MoveC(j48,j38,sp,zone,toolgg,wobjgg);
		MoveC(j50,j58,sp,zone,toolgg,wobjgg);
		MoveC(j52,j42,sp,zone,toolgg,wobjgg);
		MoveC(j54,j62,sp,zone,toolgg,wobjgg);
		MoveC(j62,j44,sp,zone,toolgg,wobjgg);
		MoveC(j70,j80,sp,zone,toolgg,wobjgg);
		MoveC(j68,j60,sp,zone,toolgg,wobjgg);
		MoveC(j66,j76,sp,zone,toolgg,wobjgg);
		MoveC(j74,j56,sp,zone,toolgg,wobjgg);
		MoveC(j56,j66,sp,zone,toolgg,wobjgg);
		MoveC(j46,mid_ropose(j47,j55,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j10,sp,zone,toolgg,wobjgg);

		MoveC(j2,mid_ropose(j11,j1,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j4,sp,zone,toolgg,wobjgg);
		MoveC(j14,mid_ropose(j13,j5,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j32,sp,zone,toolgg,wobjgg);
		MoveC(j40,mid_ropose(j31,j41,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j38,sp,zone,toolgg,wobjgg);
		MoveC(j28,mid_ropose(j29,j37,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j10,sp,zone,toolgg,wobjgg);
		MoveC(j2,mid_ropose(j11,j1,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j6,sp,zone,toolgg,wobjgg);
		MoveC(j16,mid_ropose(j15,j7,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j52,sp,zone,toolgg,wobjgg);
		MoveC(j60,mid_ropose(j51,j61,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j56,sp,zone,toolgg,wobjgg);
		MoveC(j46,mid_ropose(j47,j55,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j10,sp,zone,toolgg,wobjgg);
		MoveC(j2,mid_ropose(j11,j1,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j8,sp,zone,toolgg,wobjgg);
		MoveC(j18,mid_ropose(j17,j9,&mid, o),sp,zone,toolgg,wobjgg);
		MoveL(j72,sp2,zone,toolgg,wobjgg);
		MoveC(j80,mid_ropose(j71,j81,&mid, o),sp2,zone,toolgg,wobjgg);
		MoveL(j74,sp1,zone,toolgg,wobjgg);
		MoveC(j64,mid_ropose(j65,j73,&mid, o),sp1,zone,toolgg,wobjgg);
		MoveL(j1,sp1,NULL,toolgg,wobjgg);
		Rsleep(2000);
	}
	stop_flag=1;
	printf("ssss\n");
	Rsleep(1000000000);
	ret=EGMStop(0);
	printf("EGMStop:%d\n",ret);

	EGMDelete(0);
	printf("EGMDelete:%d\n",ret);

	Rsleep(2000);
	return 0;
}

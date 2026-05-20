
#include "HYYRobotInterface.h"
extern void clear_robot_move_error(int robot_index);
int MainModule()
{
	//启动egm功能
//	if (0!=ClientCreate("192.168.0.x", 8888, "pc"))
//	{
//		Rdebug("client connect failure!\n");
//		return -1;
//	}
	robjoint egmjoint;
	getrobjoint("jegm", &egmjoint);
	speed sp;
	getspeed("v10",&sp);
	tool to;
	gettool("tool15",&to);
	wobj wo;
	getwobj("wobj0",&wo);

	//-------------------设置允许空间---------------------------
	int ret=0;
	//设置底面
 	double centre1[3]={-10000, -10000,0};
 	double length[3]={10000,-10000,0};
 	double width[3]={-10000,10000,0};
 	double high[3]={-10000, -10000,0.080};
 	ret=ret|AddSpatialConstraintCuboid(centre1, length, width, high, _constraint_slowallow, NULL, NULL, "cuboid1");
 	
	//设置附近台阶
/*	double centreb[3]={0,0,0};
 	double centreh[3]={0,0,380};
 	ret=ret|AddSpatialConstraintCylinder(centreb, centreh, 0, 650, _constraint_slowallow, NULL, NULL, "cylinder1");

	//设置蓝色桶
	double centreb1[3]={765,-1079,0};
 	double centreh1[3]={765,-1079,597};
 	ret=ret|AddSpatialConstraintCylinder(centreb1, centreh1, 70, 90, _constraint_slowallow, NULL, NULL, "cylinder2");

	//设置红色桶
	double centreb2[3]={842,957,0};
 	double centreh2[3]={842,957,800};
 	ret=ret|AddSpatialConstraintCylinder(centreb2, centreh2, 70, 90, _constraint_slowallow, NULL, NULL, "cylinder3");
*/
	//----------------设置不允许空间---------------
	//设置底面
	double high1[3]={-10000, -10000,0.050};
 	ret=ret|AddSpatialConstraintCuboid(centre1, length, width, high1, _constraint_noallow, NULL, NULL, "cuboid2");
/*	
	//设置附近台阶
	double centrehx[3]={0,0,355};
	ret=ret|AddSpatialConstraintCylinder(centreb, centrehx, 0, 620, _constraint_noallow, NULL, NULL, "cylinder4");

	//设置蓝色桶
 	ret=ret|AddSpatialConstraintCylinder(centreb1, centreh1, 75, 85, _constraint_noallow, NULL, NULL, "cylinder5");

	//设置红色桶
 	ret=ret|AddSpatialConstraintCylinder(centreb2, centreh2, 75, 85, _constraint_noallow, NULL, NULL, "cylinder6");
	if (0!=ret)
	{
		Rdebug("AddSpatialConstraintCy failure!\n");
		return -1;
	}
*/
	OffSpatialConstraint();//关闭笛卡尔空间约束检测，允许强行恢复运动

	RESTART:
	move_start();
//	SocketSendString("i", "pc");
	moveA(&egmjoint,&sp,NULL,NULL,NULL);
	OnSpatialConstraint();//开启笛卡尔空间约束检测
//	SocketSendString("s", "pc");
	EGMCreate_c("egmapp", 0, 0, 1, 4, &to, &wo);	
	EGMRunJoint_c("egmapp", 1);
	EGMRelease_c("egmapp");

	if (!robot_move_ok())
	{
//		SocketSendString("e", "pc");
		clear_robot_move_error(0);//强行清楚错误，允许强行恢复运动
		OffSpatialConstraint();//关闭笛卡尔空间约束检测，允许强行恢复运动
		Rsleep(1000);
		goto RESTART;
	}


	DeleteAllSpatialConstraint();

	move_stop();
    return 0;
}

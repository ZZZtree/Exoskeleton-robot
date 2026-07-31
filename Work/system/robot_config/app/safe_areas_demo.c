
#include "HYYRobotInterface.h"
int MainModule()
{

	//--------------------------自定义部分-----------------------------------
	move_start();//上电，使用move指令进行运动时必须使用该语句上电
	//获取数据

	//获取笛卡尔位置
	robpose rpose1;
	getrobpose("p1", &rpose1);
	robpose rpose2;
	getrobpose("p2", &rpose2);
	robpose rpose3;
	getrobpose("p3", &rpose3);
	//获取关节位置
	robjoint joint1;
	getrobjoint("j1",&joint1);
	robjoint joint2;
	getrobjoint("j2",&joint2);
	//获取速度
	speed sp;
	getspeed("v1",&sp);
	//转弯区
	zone ze;
        getzone("z0",&ze);
	//获取工具
	tool to;
	gettool("tool11",&to);
	//获取工件
	wobj wo;
	getwobj("wobj0",&wo);


	//设置安全区
	int ret=0;
	double centre[3]={438,0,365};
	ret=AddSpatialConstraintSphere(centre, 100, _constraint_slowallow, NULL, NULL, "sphere1");//设置球体区域，性质：不允许区域，但允许缓慢移动
 	double centre1[3]={400, 100,400};
 	double length[3]={400,100,300};
 	double width[3]={400,-100,400};
 	double high[3]={700,100,400};
 	ret=AddSpatialConstraintCuboid(centre1, length, width, high, _constraint_slowallow, NULL, NULL, "cuboid1");//设置立方体，性质：不允许区域，但允许缓慢移动
 	double centreb[3]={438,0,0};
 	double centreh[3]={438,0,400};
 	ret=AddSpatialConstraintCylinder(centreb, centreh, 0, 100, _constraint_slowallow, NULL, NULL, "cylinder");//设置圆柱体性质：不允许区域，但允许缓慢移动
 	Rdebug("AddSpatialConstraintCylinder:%d\n",ret);
	robpose rpose;
	init_robpose(&rpose, centre, NULL);
	ret=moveL(&rpose,&sp,NULL,NULL,NULL);
	Rdebug("moveL:%d\n",ret);
	ret=DeleteAllSpatialConstraint();//删除所有区域

	move_stop();
    return 0;
}

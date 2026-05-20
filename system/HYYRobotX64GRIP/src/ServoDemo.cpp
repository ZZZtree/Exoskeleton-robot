#include "HYYRobotInterface.h"
#include <math.h>
#include <unistd.h>
using namespace HYYRobotBase;//仅c++需要
int ServoDemo()
{
    RTimer timer;
    initUserTimer(&timer,0,1);//定时周期为1倍总线周期
    //获取索要操作的机器人名称
    const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),0);
    double td=get_control_cycle(get_deviceName(0,NULL));//获取总线周期
    double joint[10];
    int dof=get_group_dof(robot_name);//获取机器人关节数目
    GetGroupPosition(robot_name,joint);//获取所有关节位置
    int i=0;
    for(i=0;i<dof;i++)
    {
        printf("%d:%f\n",i,joint[i]);
    }
    group_power_off(robot_name);//机器人所有轴下电
    sleep(1);
    // 上电前清故障（对所有轴发送故障复位）
    for(i=1;i<=dof;i++){
        set_axis_control(robot_name,0x80,i);
    }
    usleep(200000);
    for(i=1;i<=dof;i++){
        set_axis_control(robot_name,0,i);
    }
    sleep(1);
    int ret_power = group_power_on(robot_name);//机器人所有轴上电
    if (ret_power != 0) {
        printf("组控上电失败，返回值=%d\n", ret_power);
        return -1;
    }
    sleep(1);

    if (dof < 3) {
        printf("关节数不足3，当前dof=%d，请检查配置\n", dof);
        return -1;
    }

    // 三电机正弦运动参数
    double A=0.5;      // 振幅(rad)
    double B=1.0;      // 振幅(rad)
    double f=0.1;      // 频率(Hz)
    double t=0.0;

    // 获取初始位置作为基准
    double pos_base[10]={0};
    GetGroupPosition(robot_name,pos_base);

    // 预热：先下发当前位姿，确保稳定
    for(int k=0;k<100;k++){
        userTimer(&timer);
        SetGroupPosition(robot_name,pos_base);
    }

    double pos_target[10]={0};
    double pos_real[10]={0};
    while(robot_ok())//循环，当程序被强制停止或遇到错误时循环退出
    {
        userTimer(&timer);//定时
        pos_target[0]=A*cos(3.14*2*f*t)-A+pos_base[0];//电机1
        pos_target[1]=A*cos(3.14*2*f*t)-A+pos_base[1];//电机2
        pos_target[2]=B*cos(3.14*2*f*t)-B+pos_base[2];//电机3
        SetGroupPosition(robot_name,pos_target);//三轴同步下发
        GetGroupPosition(robot_name,pos_real);
        double tmp[6]={pos_target[0],pos_real[0],pos_target[1],pos_real[1],pos_target[2],pos_real[2]};
        RSaveDataFast1("triple_servo_data",1,100,6,tmp);
        t+=td;
    }
    return 0;
};



int CartesianServo(robpose* rp,tool* to,wobj* wo,int robot_index)
{
    R7_KINE rkine;
    const char* robot_name=get_name_robot_device(get_deviceName(0,NULL),robot_index);
    int dof=get_group_dof(robot_name);
    double joint[ROBOT_MAX_DOF];
    GetGroupTargetPosition(robot_name,joint);
    init_R7_KINE2(&rkine,joint,&dof, (TOOL*)to, (WOBJ*)wo);
    set_R7_KINE_pose(&rkine, rp->xyz, rp->kps);
    int ret=Kine_Inverse(robot_name, &rkine);
    if (0!=ret)
    {
        return ERR_INVERSEKINEMATICS;
    }
    ret=IsRobotPositionLimitMargin(robot_name,rkine.joint, 1);
    if (0!=ret)
    {
        return ERR_ROBOTJOINTLIMIT;
    }
    SetGroupPosition(robot_name,rkine.joint);
    return 0;
}

void TorqueServo()
{
    const char* add_name=get_name_additionaxis_device(get_deviceName(0,NULL), 0);

    signed char mode=10;
    set_axis_mode(add_name,mode,1);
    sleep(1);
    int axis_ID=1;//指定操作单轴id
    axis_power_on(add_name,axis_ID);//单轴上电

    RTimer timer;
    initUserTimer(&timer,0,1);//定时周期为1倍总线周期
    double A=1;double f=0.1;double t=0;
    double td=get_control_cycle(get_deviceName(0,NULL));//获取总线周期
    while (1)
    {
        userTimer(&timer);//定时
        double target_torque=A*cos(3.14*2*f*t)-A;
        SetAxisTorque(add_name, target_torque, 1);
        t+=td;
    }
}




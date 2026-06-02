#!/bin/sh
cpunum=`cat /proc/cpuinfo| grep "processor"| wc -l`
taskset -c $(($((cpunum))-1)) ./RobotMain --path /home/robot/Work/system/robot_config &




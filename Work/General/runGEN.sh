#!/bin/sh
cd /home/robot/Work/General
cd ./_run

rm -rf *.log

nice -n -20 ./DeviceDriverMain --type 0 --arg 192.168.1.100 --cycle 2000 & #infos6
#nice -n -20 ./DeviceDriverMain --type 1 --arg 192.168.1.100 --cycle 2000 & #infos4
#nice -n -20 ./DeviceDriverMain --type 2 --arg 192.168.1.100 --cycle 10000 & #xarm6
#nice -n -20 ./DeviceDriverMain --type 3 --arg 192.168.1.100 --arg2 315 & #ur 30003
#nice -n -20 ./DeviceDriverMain --type 4 --arg 192.168.1.100 --arg2 1337 --cycle 1000 & #xmate 7 axis
#nice -n -20 ./DeviceDriverMain --type 5 --arg 192.168.1.100 --arg2 2 & #ur rtde
#nice -n -20 ./DeviceDriverMain --type 6 --arg 192.168.1.100 --arg2 49152 & #kuka

exit 0



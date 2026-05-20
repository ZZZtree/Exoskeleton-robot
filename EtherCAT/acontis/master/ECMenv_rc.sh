#!/bin/sh
echo 0000:03:00.0 > /sys/bus/pci/drivers/igb/unbind
insmod /home/robot/Work/EtherCAT/acontis/atemsys/atemsys.ko

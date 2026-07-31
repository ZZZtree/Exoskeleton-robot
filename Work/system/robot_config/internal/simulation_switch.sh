#!/bin/sh
pkill sem
name1=$(pidof IgHEtherCATMaster)
name2=$(pidof EtherCATMaster)
name3=$(pidof DeviceDriverMain)
if [ -n "$name1" ] || [ -n "$name2" ] || [ -n "$name3" ]; then
exit 0
else
exit 1
fi


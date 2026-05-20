#!/bin/sh
cd /home/robot/Work/CanOpen
cp ./eni/* ./_run
cd ./_run

rm -rf *.log

nice -n -20 ./CanOpenMaster --task run --file ./eni.xml  &

exit 0






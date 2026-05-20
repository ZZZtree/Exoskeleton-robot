#!/bin/sh
cd /home/robot/Work/EtherCAT/igh
cp ./eni/* ./_run
cd ./_run

rm -rf *.log

nice -n -20 ./IgHEtherCATMaster --task config --file ./$1 --affinity 1

exit 0






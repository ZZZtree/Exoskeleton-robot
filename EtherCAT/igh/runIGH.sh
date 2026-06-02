#!/bin/sh
cd /home/robot/Work/EtherCAT/igh
cp ./eni/* ./_run
cd ./_run

rm -rf *.log

nice -n -20 ./IgHEtherCATMaster --task run --file ./eni.xml --affinity 1 --log 3  &

exit 0






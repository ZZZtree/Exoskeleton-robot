#/bin/sh
cd /home/robot/Work/EtherCAT/acontis/master
cp ./eni/* ./_run
cd ./_run

rm -rf *.log
rm -rf *.csv

nice -n -20 ./EtherCATMaster -f eni.xml -i8254x 3 1 -b 1000 -v 3 -perf -t 0  -a 1 &

exit 0




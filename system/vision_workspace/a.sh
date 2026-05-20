g++ yolov5_ncnn_d435i.cpp -o yolov5_ncnn_d435i \
    -I/home/robot/ncnn/build/install/include \
    -I/home/robot/ncnn/build/install/include/ncnn \
    -I/usr/include/opencv4 \
    -L/home/robot/ncnn/build/install/lib \
    -Wl,-rpath,/home/robot/ncnn/build/install/lib \
    -lncnn -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_video \
    -lm -lpthread -fopenmp -O3
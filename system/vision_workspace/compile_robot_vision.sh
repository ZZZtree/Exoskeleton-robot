#!/bin/bash
# 编译 robot_vision (模块化版本: RTLinux 电机控制 + YOLOv5 视觉)
# 主线程: SCHED_FIFO 实时电机控制
# 视觉线程: SCHED_OTHER 非实时目标检测

set -e

echo "=========================================="
echo "编译: robot_vision (RT电机 + 视觉检测)"
echo "=========================================="

# 依赖路径
HYY_ROBOT_INCLUDE="/home/robot/Work/system/HYYRobotX64GRIP/include"
HYY_ROBOT_SRC="/home/robot/Work/system/HYYRobotX64GRIP/src"
HYY_ROBOT_LIB="/home/robot/Work/system/HYYRobotX64GRIP/lib"

# NCNN 路径
NCNN_INCLUDE="/home/robot/ncnn/build/install/include"
NCNN_INCLUDE_NCNN="/home/robot/ncnn/build/install/include/ncnn"
NCNN_LIB="/home/robot/ncnn/build/src"
OPENCV_INCLUDE="/usr/include/opencv4"

# YOLOv5 模型路径
YOLOV5_PATH="/home/robot/vision_workspace"

# 源文件目录
SRC_DIR="src"
INCLUDE_DIR="include"

INCLUDES="-I${INCLUDE_DIR} \
-I${INCLUDE_DIR}/ncnn \
-I${HYY_ROBOT_INCLUDE} \
-I${HYY_ROBOT_INCLUDE}/user \
-I${HYY_ROBOT_INCLUDE}/Move \
-I${HYY_ROBOT_INCLUDE}/Comm \
-I${HYY_ROBOT_INCLUDE}/Base \
-I${HYY_ROBOT_INCLUDE}/EGM \
-I${HYY_ROBOT_INCLUDE}/DeviceDriver \
-I${HYY_ROBOT_INCLUDE}/Model \
-I${HYY_ROBOT_INCLUDE}/Sensor \
-I${HYY_ROBOT_INCLUDE}/Grip \
-I${HYY_ROBOT_INCLUDE}/Tool \
-I${HYY_ROBOT_INCLUDE}/Technology \
-I${HYY_ROBOT_INCLUDE}/Teach \
-I${NCNN_INCLUDE} \
-I${NCNN_INCLUDE_NCNN} \
-I${OPENCV_INCLUDE} \
-I${YOLOV5_PATH}"

# 源文件列表
SOURCES="main.cpp \
${SRC_DIR}/common.cpp \
${SRC_DIR}/stats.cpp \
${SRC_DIR}/rt_control.cpp \
${SRC_DIR}/detector.cpp \
${SRC_DIR}/camera.cpp \
${SRC_DIR}/vision.cpp \
${HYY_ROBOT_SRC}/ServoDemo.cpp \
${HYY_ROBOT_SRC}/SocketDemo.cpp"

echo "编译源文件..."
echo "  main.cpp"
echo "  src/common.cpp"
echo "  src/stats.cpp"
echo "  src/rt_control.cpp"
echo "  src/detector.cpp"
echo "  src/camera.cpp"
echo "  src/vision.cpp"

# 编译命令
g++ ${SOURCES} \
    -o robot_vision \
    -std=c++17 \
    -Wall -Wextra \
    -D_GNU_SOURCE \
    -O2 \
    ${INCLUDES} \
    -L"/home/robot/Work/system/HYYRobotX64GRIP/lib" \
    -lRobotControl \
    -lHYYRobotTechnology \
    -lmodbus \
    -lnlopt \
    -lpthread \
    -lrt \
    -ldl \
    -lxml2 \
    -L"${NCNN_LIB}" \
    -lncnn \
    -lopencv_core \
    -lopencv_imgproc \
    -lopencv_highgui \
    -lopencv_video \
    -lopencv_videoio \
    -lopencv_imgcodecs \
    -lm \
    -fopenmp

echo ""
echo "=========================================="
echo "编译完成!"
echo "可执行文件: robot_vision"
echo "=========================================="

ls -lh robot_vision

echo ""
echo "运行命令:"
echo "  cd /home/robot/Work/system/vision_workspace"
echo "  sudo ./robot_vision --iscopy true"
echo ""
echo "程序说明:"
echo "  - 主线程: RTLinux SCHED_FIFO (优先级 90) 实时电机控制"
echo "  - 视觉线程: SCHED_OTHER 非实时 YOLOv5 目标检测"
echo "  - 两线程通过线程安全队列通信"
echo ""
echo "模块说明:"
echo "  - common.h/cpp:   共用头文件和全局变量"
echo "  - stats.h/cpp:    CPU和性能统计"
echo "  - rt_control.h/cpp: RTLinux实时任务和电机控制"
echo "  - detector.h/cpp: YOLOv5检测和ByteTrack追踪"
echo "  - camera.h/cpp:   V4L2相机和Epoll管理器"
echo "  - vision.h/cpp:   视觉线程主函数"
echo ""
echo "注意: 需要 root 权限来设置实时调度策略"

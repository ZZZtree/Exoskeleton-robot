cmake_minimum_required(VERSION 3.10)
project(robot_vision LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/ncnn
)

# 源文件
set(SOURCES
    src/common.cpp
    src/stats.cpp
    src/rt_control.cpp
    src/detector.cpp
    src/camera.cpp
    src/vision.cpp
    main.cpp
)

# 可执行文件
add_executable(robot_vision ${SOURCES})

# 链接库
target_link_libraries(robot_vision
    pthread
    ncnn
    opencv_core
    opencv_imgproc
    opencv_video
    opencv_highgui
    opencv_imgcodecs
    video
)

# 系统库
target_link_libraries(robot_vision
    rt
    m
)

# 编译选项
target_compile_options(robot_vision PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-g -O0>
)

# 安装目标
install(TARGETS robot_vision DESTINATION bin)

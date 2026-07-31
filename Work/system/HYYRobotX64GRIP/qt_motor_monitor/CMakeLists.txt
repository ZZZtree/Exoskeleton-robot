cmake_minimum_required(VERSION 3.16)
project(MotorMonitor VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt5
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Charts Network)

# 包含共享内存数据定义头文件
include_directories(
    ${CMAKE_SOURCE_DIR}/../include
)

# 源文件
set(SOURCES
    main.cpp
    mainwindow.cpp
    chartwidget.cpp
    udp_sender.cpp
    udp_receiver.cpp
)

set(HEADERS
    mainwindow.h
    chartwidget.h
    udp_sender.h
    udp_receiver.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
    Qt5::Core
    Qt5::Widgets
    Qt5::Charts
    Qt5::Network
    rt  # POSIX 共享内存需要 librt
)

# 确保 Qt Charts 头文件路径被正确传递给 MOC 和编译器
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${Qt5Charts_INCLUDE_DIRS}
)

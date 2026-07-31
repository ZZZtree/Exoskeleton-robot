cmake_minimum_required(VERSION 3.16)
project(ServoVisionRT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ──────────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wno-unused-parameter
    -O2
    -g
)

# ── Find packages ───────────────────────────────────────────────────────────
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui videoio)
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)

# ── Paths ──────────────────────────────────────────────────────────────────
set(NCNN_ROOT     "/home/robot/ncnn/build/install")
set(NCNN_LIB       "${NCNN_ROOT}/lib/libncnn.a")
set(HYY_INCLUDE    "/home/robot/Work/system/HYYRobotX64GRIP/include")
# Library directory: prefer system /lib/ so both RobotControl and
# HYYRobotTechnology are the same version (Sep 24) as HYYRobotMain.
# This avoids version-mismatch errors seen when RobotControl.so (Nov 17)
# and HYYRobotTechnology.so (Sep 24) are mixed.
set(HYY_LIB_SYSTEM "/lib")
set(VISION_WORKSPACE "${CMAKE_SOURCE_DIR}/../vision_workspace")

# ── Build ───────────────────────────────────────────────────────────────────
add_executable(servo_vision_rt
    src/servo_vision_rt.cpp
)

target_include_directories(servo_vision_rt PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${NCNN_ROOT}/include
    ${HYY_INCLUDE}
)

target_link_directories(servo_vision_rt PRIVATE ${HYY_LIB_SYSTEM})

target_link_libraries(servo_vision_rt PRIVATE
    Threads::Threads
    OpenMP::OpenMP_CXX
    ${OpenCV_LIBS}
    ${NCNN_LIB}
    RobotControl
    HYYRobotTechnology
    modbus
    nlopt
    m
    dl
    pthread
    rt
)
set_target_properties(servo_vision_rt PROPERTIES LINK_FLAGS "-rdynamic")

# ── Install ─────────────────────────────────────────────────────────────────
install(TARGETS servo_vision_rt RUNTIME DESTINATION bin)

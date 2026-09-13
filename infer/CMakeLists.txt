cmake_minimum_required(VERSION 3.10)
project(yolov5_infer)

set(CMAKE_CXX_STANDARD 14)

# 地平线SDK路径
set(HORIZON_SDK /root/.horizon/ddk/j5_x86_64_gcc_5.4.0/dnn_x86)

# 头文件路径
include_directories(${HORIZON_SDK}/include/dnn)

# 库文件路径
link_directories(${HORIZON_SDK}/lib)

# OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(infer_demo infer_demo.cpp)

# 链接库：dnn(BPU推理) + hbdk_sim_x86(x86仿真) + OpenCV
target_link_libraries(infer_demo
    dnn
    hbdk_sim_x86
    ${OpenCV_LIBS}
    pthread
)

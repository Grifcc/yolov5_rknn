cmake_minimum_required(VERSION 3.4.1)

project(rknn_yolov5)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--allow-shlib-undefined")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wl,--allow-shlib-undefined -g")

# Set Compiler
set(GCC_PATH /opt/aarch64-rockchip-linux-gnu/bin)
set(GCC_COMPILER  ${GCC_PATH}/aarch64-rockchip-linux-gnu)
set (CMAKE_C_COMPILER ${GCC_COMPILER}-gcc)
set (CMAKE_CXX_COMPILER ${GCC_COMPILER}-g++)

set(LIB_ARCH aarch64)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/build/bin)

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
# set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)

# rknn api
set(RKNN_API_PATH ${CMAKE_SOURCE_DIR}/runtime/RK3588/${CMAKE_SYSTEM_NAME}/librknn_api)
set(RKNN_RT_LIB ${RKNN_API_PATH}/${LIB_ARCH}/librknnrt.so)
include_directories(${RKNN_API_PATH}/include)
include_directories(${CMAKE_SOURCE_DIR}/3rdparty)

# opencv
set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/3rdparty/opencv/opencv-linux-aarch64/share/OpenCV)
find_package(OpenCV REQUIRED)

# RGA
set(RGA_PATH ${CMAKE_SOURCE_DIR}/3rdparty/rga/RK3588)
set(RGA_LIB ${RGA_PATH}/lib/Linux/${LIB_ARCH}/librga.so)
include_directories( ${RGA_PATH}/include)


set(CMAKE_INSTALL_RPATH "lib")

include_directories( ${CMAKE_SOURCE_DIR}/include/)

add_executable(rknn_yolov5
        src/main.cc
        src/postprocess.cc
)

target_link_libraries(rknn_yolov5
  ${RKNN_RT_LIB}
  ${RGA_LIB}
  ${OpenCV_LIBS}
)

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)
install(TARGETS rknn_yolov5 DESTINATION ./)

install(PROGRAMS ${RKNN_RT_LIB} DESTINATION lib)
install(PROGRAMS ${RGA_LIB} DESTINATION lib)
install(DIRECTORY model DESTINATION ./)
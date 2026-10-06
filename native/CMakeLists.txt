cmake_minimum_required(VERSION 3.16)
project(cap_detect LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(VIPLITE_INCLUDE_DIR vip_lite.h PATHS /usr/include /usr/local/include)
find_library(VIPHAL_LIB VIPhal PATHS /usr/lib /usr/local/lib)
find_library(NBGLINKER_LIB NBGlinker PATHS /usr/lib /usr/local/lib)
find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)

set(CAP_DETECT_SOURCES
    app/main.cpp
    npu_runner/npu_runner.cpp
    preprocess/preprocess.cpp
    postprocess/yolov8_post.cpp
)

if(OpenCV_FOUND)
    message(STATUS "OpenCV enabled: ${OpenCV_VERSION}")
    list(APPEND CAP_DETECT_SOURCES camera/camera_v4l2.cpp)
    add_compile_definitions(CAP_USE_OPENCV=1)
else()
    message(WARNING "OpenCV not found; camera input disabled")
endif()

if(VIPLITE_INCLUDE_DIR AND VIPHAL_LIB AND NBGLINKER_LIB)
    message(STATUS "VIPLite enabled: ${VIPLITE_INCLUDE_DIR}")
    list(APPEND CAP_DETECT_SOURCES
        third_party/awnn_viplite/awnn_lib.c
        third_party/awnn_viplite/awnn_quantize.c
    )
    add_compile_definitions(CAP_USE_VIPLITE=1)
else()
    message(WARNING "VIPLite headers/libs not found; building stub NpuRunner")
endif()

add_executable(cap_detect ${CAP_DETECT_SOURCES})

target_include_directories(cap_detect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/awnn_viplite
)

if(VIPLITE_INCLUDE_DIR)
    target_include_directories(cap_detect PRIVATE ${VIPLITE_INCLUDE_DIR})
endif()

if(OpenCV_FOUND)
    target_include_directories(cap_detect PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

target_compile_options(cap_detect PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(VIPLITE_INCLUDE_DIR AND VIPHAL_LIB AND NBGLINKER_LIB)
    target_link_libraries(cap_detect PRIVATE ${NBGLINKER_LIB} ${VIPHAL_LIB} pthread m)
endif()

if(OpenCV_FOUND)
    target_link_libraries(cap_detect PRIVATE ${OpenCV_LIBS})
endif()

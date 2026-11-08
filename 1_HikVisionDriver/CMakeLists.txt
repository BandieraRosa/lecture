cmake_minimum_required(VERSION 3.18)
project(hik_camera LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MVS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/hikSDK")
set(MVS_LIBRARY_DIR "${MVS_SDK_DIR}/lib/amd64")
find_package(OpenCV REQUIRED COMPONENTS core)

add_library(hik_camera SHARED src/camera.cpp)
add_library(hik_camera::hik_camera ALIAS hik_camera)
target_compile_features(hik_camera PUBLIC cxx_std_17)
target_include_directories(
  hik_camera
  PUBLIC include
  PRIVATE "${MVS_SDK_DIR}/include")
target_link_libraries(
  hik_camera
  PUBLIC opencv_core
  PRIVATE "${MVS_LIBRARY_DIR}/libMvCameraControl.so" "-Wl,--no-as-needed"
          "${MVS_LIBRARY_DIR}/libMvUsb3vTL.so" "-Wl,--as-needed")
target_compile_options(hik_camera PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(hik_camera PRIVATE "LINKER:--disable-new-dtags")

add_executable(hik_capture examples/capture.cpp)
target_link_libraries(hik_capture PRIVATE hik_camera)

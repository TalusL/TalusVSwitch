cmake_minimum_required(VERSION 3.22)
project(tunnel)


add_subdirectory(libtuntap)
add_subdirectory(toolkit)

set(CMAKE_CXX_STANDARD 20)

include_directories(libtuntap/bindings/cpp)
include_directories(libtuntap)
include_directories(fec)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

add_executable(tunnel main.cpp)
target_link_libraries(tunnel tuntap++ tuntap z ZLToolKit_static)

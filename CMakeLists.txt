cmake_minimum_required(VERSION 3.22)
project(TalusVSwitch)


add_subdirectory(libtuntap)
add_subdirectory(toolkit)

set(CMAKE_CXX_STANDARD 20)

SET(BUILD_TESTING OFF)
include_directories(libtuntap/bindings/cpp)
include_directories(libtuntap)
include_directories(fec)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

if (DEBUG)
    add_definitions(-DDEBUG)
endif ()

add_executable(TalusVSwitch
        src/main.cpp
        src/VSwitch.cpp
        src/VSCtrlHelper.cpp
)
target_link_libraries(TalusVSwitch tuntap++ tuntap z ZLToolKit_static)

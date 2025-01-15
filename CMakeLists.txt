cmake_minimum_required(VERSION 3.22)
project(TalusVSwitch)


add_subdirectory(libtuntap)
add_subdirectory(toolkit)

set(CMAKE_CXX_STANDARD 20)

SET(BUILD_TESTING OFF)
include_directories(libtuntap/bindings/cpp)
include_directories(libtuntap)


SET(JSONCPP_WITH_TESTS OFF)
SET(JSONCPP_WITH_POST_BUILD_UNITTEST OFF)
add_subdirectory(jsoncpp)
include_directories(jsoncpp/include)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

if (DEBUG)
    add_definitions(-DDEBUG)
endif ()

add_executable(TalusVSwitch
        src/main.cpp
        src/VSwitch.cpp
        src/VSCtrlHelper.cpp
        src/webapi/ApiServer.cpp
)
target_link_libraries(TalusVSwitch tuntap++ tuntap z ZLToolKit_static jsoncpp_static)

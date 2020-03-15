cmake_minimum_required(VERSION 3.15)
project(delaytool)

set(CMAKE_CXX_STANDARD 17)
add_definitions(-O2)
add_definitions(-Wall)
#add_definition(-Werror)

add_library(tinyxml2 STATIC source/tinyxml2/tinyxml2.cpp)

add_executable(delaytool source/algo.cpp source/algo.h)
target_link_libraries(delaytool tinyxml2)
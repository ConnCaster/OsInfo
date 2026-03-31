cmake_minimum_required(VERSION 3.31)
project(test)

set(CMAKE_CXX_STANDARD 17)

add_executable(test_new main_new.cpp)
add_executable(test main.cpp)
add_executable(lshw main_lshw.cpp)
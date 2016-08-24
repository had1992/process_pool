cmake_minimum_required(VERSION 3.6)
project(process_pool)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES main.cpp process.h processpool.h base_event.h)
add_executable(process_pool ${SOURCE_FILES})
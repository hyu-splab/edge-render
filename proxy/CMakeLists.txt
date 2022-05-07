cmake_minimum_required(VERSION 3.22)
project(EdgeRenderShared)

set(CMAKE_CXX_STANDARD 14)

add_library(EdgeRenderShared SHARED proxy.cpp)

cmake_minimum_required (VERSION 3.0.0)

find_package (OpenMP REQUIRED)
find_package (Threads REQUIRED)

set (CMAKE_BUILD_TYPE "Debug")
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS} ${CMAKE_THREAD_LIBS_INIT}")

add_executable (sync sync.cpp)

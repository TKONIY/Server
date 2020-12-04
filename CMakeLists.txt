cmake_minimum_required(VERSION 3.17)
project(TodoServer)

set(CMAKE_CXX_STANDARD 14)
add_executable(TodoServer src/main.cpp lib/Server.cpp lib/Server.h lib/Logger.h lib/ThreadPool.cpp lib/ThreadPool.h)
target_link_libraries(${PROJECT_NAME} -lpthread)
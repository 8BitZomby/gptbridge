cmake_minimum_required(VERSION 3.20)

project(gptbridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(gptbridge
    src/main.cpp
)

target_include_directories(gptbridge PRIVATE include)

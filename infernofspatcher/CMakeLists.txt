cmake_minimum_required(VERSION 3.18)

project(InfernoFSPatcher LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION YES)

add_executable(inferno_fs_patcher src/main.cpp)
target_compile_options(inferno_fs_patcher PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wold-style-cast -Werror)

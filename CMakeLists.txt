cmake_minimum_required(VERSION 3.29)

set(CMAKE_CXX_COMPILER clang++.exe)
set(CMAKE_C_COMPILER clang.exe)
set(CMAKE_LINKER lld-link.exe)
#set(CMAKE_CXX_CLANG_TIDY clang-tidy.exe)

add_compile_options(-fansi-escape-codes -fcolor-diagnostics)

project(my-asio-samples)

add_subdirectory(asiosdk/)

add_executable(AsioContext AsioContext.cpp)
target_link_libraries(AsioContext PRIVATE asiodrivers asio)
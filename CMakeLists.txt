cmake_minimum_required(VERSION 3.29)

set(CMAKE_CXX_COMPILER clang++.exe)
set(CMAKE_C_COMPILER clang.exe)
set(CMAKE_LINKER lld-link.exe)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 
#set(CMAKE_CXX_CLANG_TIDY clang-tidy.exe)

add_compile_options(-fansi-escape-codes -fcolor-diagnostics)

project(gigon-core)

include_directories(Inc/)

add_subdirectory(lib/asiosdk/)
add_subdirectory(lib/PortableEndian)

add_library(AsioContext Src/AsioContext.cpp)
target_link_libraries(AsioContext PUBLIC asiodrivers asio)

include(Examples/examples.cmake)
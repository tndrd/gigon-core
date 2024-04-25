cmake_minimum_required(VERSION 3.29)

set(CMAKE_CXX_COMPILER clang++.exe)
set(CMAKE_C_COMPILER clang.exe)
set(CMAKE_LINKER lld-link.exe)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 
#set(CMAKE_CXX_CLANG_TIDY clang-tidy.exe)

add_compile_options(-fansi-escape-codes -fcolor-diagnostics)

project(gigon-core)

include_directories(Inc/)

add_subdirectory(Lib/asiosdk/)
add_subdirectory(Lib/PortableEndian/)
add_subdirectory(Lib/vstsdk2.4/)

add_library(AsioContext Src/AsioContext.cpp)
target_link_libraries(AsioContext PUBLIC asiodrivers asio)

add_executable(Vst2Effect Src/Vst2Effect.cpp)
target_link_libraries(Vst2Effect PUBLIC AEffectX)

include(Examples/examples.cmake)
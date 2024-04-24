cmake_minimum_required(VERSION VERSION 3.29)
project(portable-endian)

add_library(PortableEndian INTERFACE PortableEndian.h)
target_include_directories(PortableEndian INTERFACE .)
cmake_minimum_required(VERSION 3.29)
project(asiosdk)

set(ASIO_HEADERS_PATHS host/pc/ host/ common/)

include_directories(${ASIO_HEADERS_PATHS})

# Common libs
add_library(asio common/asio.cpp)
#add_library(asiodrvr common/asiodrvr.cpp)
add_library(combase common/combase.cpp)
add_library(debugmessage common/debugmessage.cpp)

#add_library(dllentry common/dllentry.cpp)
#target_link_libraries(dllentry PUBLIC combase)

#add_library(register common/register.cpp)

# Host libs
add_library(asiolist host/pc/asiolist.cpp)
#add_library(AsioConvertSamples host/AsioConvertSamples.cpp)
add_library(asiodrivers host/asiodrivers.cpp)

target_link_libraries(asiodrivers PUBLIC asiolist)
target_include_directories(asiodrivers PUBLIC ${ASIO_HEADERS_PATHS})
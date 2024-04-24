cmake_minimum_required(VERSION 3.29)
project(vstsdk2.4)

add_library(AEffectX INTERFACE pluginterfaces/vst2.x/aeffectx.h)
target_include_directories(AEffectX INTERFACE pluginterfaces/vst2.x/)
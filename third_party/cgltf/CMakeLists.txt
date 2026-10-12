cmake_minimum_required(VERSION 3.22.0)

# cgltf master at commit 85cd623 of 2026-02-02, newer than release 1.15 (https://github.com/jkuhlmann/cgltf)
project(cgltf LANGUAGES C CXX)
add_library(cgltf INTERFACE)
target_sources(cgltf INTERFACE
  cgltf.cpp
  cgltf.h
)

target_include_directories(cgltf INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

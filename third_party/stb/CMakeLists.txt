cmake_minimum_required(VERSION 3.22.0)

# stb_image.h v2.30 (https://github.com/nothings/stb)
project(stb LANGUAGES C CXX)
add_library(stb INTERFACE)
target_sources(stb INTERFACE
  stb_image.cpp
  stb_image.h
)

target_include_directories(stb INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

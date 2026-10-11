cmake_minimum_required(VERSION 3.22.0)

# volk, tag vulkan-sdk-1.4.350.1 (https://github.com/zeux/volk)
project(volk LANGUAGES CXX)
add_library(volk INTERFACE)
target_sources(volk INTERFACE
  volk.cpp
  volk/volk.h
)

target_include_directories(volk SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(volk INTERFACE vulkan_headers)

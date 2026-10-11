cmake_minimum_required(VERSION 3.22.0)

# Vulkan-Headers C headers, tag vulkan-sdk-1.4.350.1 (https://github.com/KhronosGroup/Vulkan-Headers)
project(vulkan_headers LANGUAGES CXX)
add_library(vulkan_headers INTERFACE)

target_include_directories(vulkan_headers SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

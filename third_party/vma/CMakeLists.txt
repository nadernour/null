cmake_minimum_required(VERSION 3.22.0)

# VulkanMemoryAllocator, tag v3.3.0 (https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
project(vma LANGUAGES CXX)
add_library(vma INTERFACE)
target_sources(vma INTERFACE
  vma.cpp
  vma/vk_mem_alloc.h
)

target_include_directories(vma SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vma INTERFACE vulkan_headers volk)

cmake_minimum_required(VERSION 3.22.0)

# vk_enum_string_helper.h and vk_format_utils.h from Vulkan-Utility-Libraries, tag vulkan-sdk-1.4.350.1
# (https://github.com/KhronosGroup/Vulkan-Utility-Libraries)
project(vulkan_utility LANGUAGES CXX)
add_library(vulkan_utility INTERFACE)

target_include_directories(vulkan_utility SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vulkan_utility INTERFACE vulkan_headers)

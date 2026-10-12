cmake_minimum_required(VERSION 3.22.0)

# Dear ImGui 1.93.0 WIP, master at commit 2859f67 of 2026-10-07 (https://github.com/ocornut/imgui)
project(imgui LANGUAGES CXX)

add_library(imgui INTERFACE)
target_sources(imgui INTERFACE
  imgui.cpp
  imgui_draw.cpp
  imgui_tables.cpp
  imgui_widgets.cpp
  imgui_demo.cpp
  backends/imgui_impl_glfw.cpp
  backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/backends
)
target_compile_definitions(imgui INTERFACE
  IMGUI_IMPL_VULKAN_NO_PROTOTYPES
  IMGUI_IMPL_VULKAN_USE_VOLK
  IMGUI_IMPL_VULKAN_VOLK_FILENAME=<volk/volk.h>
)

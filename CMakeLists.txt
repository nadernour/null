cmake_minimum_required(VERSION 3.24)
project(null VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # generate compile_commands.json for nvim's lsp
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standalone/Packaged build option. Defaults to OFF for all configurations.
option(NE_PACKAGED_BUILD "Build the application as a packaged standalone build" OFF)

set(EngineTarget "null_engine")
set(ThirdPartyDir "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(SourceDir "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(BinDir "${CMAKE_BINARY_DIR}/bin")
set(CompiledShaderRelDir "compiled_shaders")
set(BinShaderDir "${BinDir}/${CompiledShaderRelDir}")

# ----------------------------------------------------------------------
# Thirdparty
# ----------------------------------------------------------------------
# Vulkan headers, utility headers, volk, and VMA, vendored at Vulkan SDK 1.4.350.1
add_subdirectory("${ThirdPartyDir}/vulkan")
add_subdirectory("${ThirdPartyDir}/vulkan_utility")
add_subdirectory("${ThirdPartyDir}/volk")
add_subdirectory("${ThirdPartyDir}/vma")

set(GLFW_BUILD_DOCS OFF) # The docs folder is not vendored
add_subdirectory("${ThirdPartyDir}/glfw")
add_subdirectory("${ThirdPartyDir}/cgltf")
add_subdirectory("${ThirdPartyDir}/stb")
add_subdirectory("${ThirdPartyDir}/imgui")

# ----------------------------------------------------------------------
# The Main App
# ----------------------------------------------------------------------
add_executable(${EngineTarget})

file(GLOB_RECURSE ENGINE_SOURCE_FILES
  LIST_DIRECTORIES false
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  CONFIGURE_DEPENDS
  src/*.h src/*.cpp
)

file(GLOB_RECURSE ENGINE_SHADERS
  LIST_DIRECTORIES false
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  CONFIGURE_DEPENDS
  shaders/*.slang
)

if(MSVC)
  string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}") # Disable Exceptions
endif()

target_sources(${EngineTarget} PRIVATE ${ENGINE_SOURCE_FILES} ${ENGINE_SHADERS})
set_source_files_properties(${ENGINE_SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(${EngineTarget} PRIVATE ${SourceDir})
target_link_libraries(${EngineTarget} PRIVATE
  vulkan_headers
  vulkan_utility
  volk
  vma
  glfw
  cgltf
  stb
  imgui
)
target_compile_definitions(${EngineTarget} PRIVATE
  $<$<CONFIG:Debug>:NE_BUILD_DEBUG>
  $<$<CONFIG:RelWithDebInfo>:NE_BUILD_DEVELOPMENT>
  $<$<CONFIG:Release>:NE_BUILD_SHIPPING>

  $<$<PLATFORM_ID:Windows>:NE_PLATFORM_WINDOWS>
  $<$<PLATFORM_ID:Linux>:NE_PLATFORM_LINUX>
  $<$<PLATFORM_ID:Darwin>:NE_PLATFORM_APPLE>

  # Asset referencing configuration
  NE_CONTENT_DIR="$<IF:$<BOOL:${NE_PACKAGED_BUILD}>,content,../../../content>"
  NE_SHADER_DIR="${CompiledShaderRelDir}"
  NE_SAVED_DIR="saved"

  # Disable Exceptions
  $<$<CXX_COMPILER_ID:MSVC>:_HAS_EXCEPTIONS=0>

  VK_NO_PROTOTYPES
  VMA_STATIC_VULKAN_FUNCTIONS=0
  VMA_DYNAMIC_VULKAN_FUNCTIONS=0
  GLFW_INCLUDE_NONE # Vulkan only: glfw3.h skips the OpenGL headers (libgl-dev on Linux)
)

target_compile_options(${EngineTarget} PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /Zc:preprocessor /MP>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wpedantic;-fno-exceptions>
)

set_target_properties(${EngineTarget} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BinDir})

# ----------------------------------------------------------------------
# Shaders
# ----------------------------------------------------------------------
# Slang shader compiler, the archive for this platform
set(SlangArchive_Windows "windows-x86_64.zip")
set(SlangArchive_Linux "linux-x86_64-glibc-2.28.tar.gz") # No LLVM backend: 20 MB instead of 74 MB
include(FetchContent)
FetchContent_Declare(slang URL "https://github.com/shader-slang/slang/releases/download/v2026.19/slang-2026.19-${SlangArchive_${CMAKE_HOST_SYSTEM_NAME}}")
FetchContent_MakeAvailable(slang)
set(SlangcExecutable "${slang_SOURCE_DIR}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "slangc: ${SlangcExecutable}")

file(MAKE_DIRECTORY ${BinShaderDir})
set(CompiledShaders "")

foreach(shader_file ${ENGINE_SHADERS})
  get_filename_component(shader_name ${shader_file} NAME_WE)
  set(spirv_file "${BinShaderDir}/${shader_name}.spv")
  set(absolute_shader_path "${CMAKE_CURRENT_SOURCE_DIR}/${shader_file}")
  message(STATUS "SLANGC compile: ${absolute_shader_path}")
  add_custom_command(
    OUTPUT ${spirv_file}
    COMMAND ${SlangcExecutable} ${absolute_shader_path}
            -target spirv
            -profile spirv_1_4
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -o ${spirv_file}
    DEPENDS ${absolute_shader_path} ${SlangcExecutable}
    COMMENT "compiling shader ${shader_name} to SPIR-V"
  )
  list(APPEND CompiledShaders ${spirv_file})
endforeach()

add_custom_target(Shaders DEPENDS ${CompiledShaders})
add_dependencies(${EngineTarget} Shaders)

# Post-build asset copying for packaged builds
if(NE_PACKAGED_BUILD)
  add_custom_command(
    TARGET ${EngineTarget} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/content"
            "${BinDir}/content"
    COMMENT "Copying assets to build binary directory for packaged build..."
  )
endif()

message(STATUS "✅ ${EngineTarget} configured with C++${CMAKE_CXX_STANDARD}")

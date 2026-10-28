cmake_minimum_required(VERSION 3.22.0)

# MikkTSpace master at commit 3e895b4 of 2020-03-25, its latest (https://github.com/mmikk/MikkTSpace)
project(mikktspace LANGUAGES C)

# A C library of its own, like glfw: its SEdge union holds an anonymous struct, which C11 allows but C99 and C++ take
# only as an extension, so compiled into the engine as C++ it would trip -Wpedantic
add_library(mikktspace STATIC
  mikktspace.c
  mikktspace.h
)
set_target_properties(mikktspace PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
)

target_include_directories(mikktspace PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

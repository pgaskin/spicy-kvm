cmake_minimum_required(VERSION 3.5)
project(lg_common LANGUAGES C)
include_directories(${PROJECT_SOURCE_DIR}/include)
add_definitions(-D_GNU_SOURCE)
add_library(lg_common STATIC
    src/debug.c
    src/debug_linux.c
    src/ringbuffer.c
    src/stringlist.c
    src/vector.c
)
target_include_directories(lg_common INTERFACE include PRIVATE src)

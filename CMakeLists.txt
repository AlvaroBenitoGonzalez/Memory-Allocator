cmake_minimum_required(VERSION 3.10)
project(MemoryAllocator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)   # necesario para asm, rand_r, __thread (gnu11)

# Por defecto, compilar optimizado (los benchmarks sin -O2 no significan nada)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")

find_package(Threads REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bench)

add_library(allocator STATIC src/allocator.c)
target_include_directories(allocator PUBLIC src)
target_compile_options(allocator PRIVATE -Wall -Wextra)
target_link_libraries(allocator PUBLIC Threads::Threads)

add_executable(b_myalloc bench/bench.c)
target_link_libraries(b_myalloc PRIVATE allocator)

add_executable(b_malloc bench/bench.c)
target_compile_definitions(b_malloc PRIVATE USE_SYSTEM)
target_link_libraries(b_malloc PRIVATE Threads::Threads)
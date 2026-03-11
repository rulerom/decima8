cmake_minimum_required(VERSION 3.15)
project(d8_bake_gen C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Bake Generator Library
add_library(d8_bake_gen STATIC
    bake_gen.c
)

target_include_directories(d8_bake_gen PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Example/Test executable
add_executable(bake_gen_test
    test_bake_gen.c
)

target_link_libraries(bake_gen_test d8_bake_gen)

# Benchmark executable
add_executable(d8_bake_bench
    bench.c
)

target_link_libraries(d8_bake_bench d8_bake_gen)

# Install
install(TARGETS d8_bake_gen
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES bake_gen.h
    DESTINATION include/d8
)

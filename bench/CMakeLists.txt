cmake_minimum_required(VERSION 3.15)
project(d8_swarm_bench C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Swarm Benchmark executable
add_executable(d8_swarm_bench
    swarm_bench.c
)

target_link_libraries(d8_swarm_bench
    d8_core
)

target_include_directories(d8_swarm_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Bake Generator Benchmark executable
add_executable(d8_bake_gen_bench
    bake_gen_bench.c
)

target_link_libraries(d8_bake_gen_bench
    d8_bake_gen
)

target_include_directories(d8_bake_gen_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bake_gen
)

# Install
install(TARGETS d8_swarm_bench
    RUNTIME DESTINATION bin
)

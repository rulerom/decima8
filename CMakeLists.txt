cmake_minimum_required(VERSION 3.15)

project(decima8-core
  VERSION 0.2.0
  DESCRIPTION "Decima-8 Neuromorphic Core (Open Source)"
  LANGUAGES C
)

# -------- Global settings --------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Static runtime for MSVC
if (MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # Enable C11 atomics support
  add_compile_options(/std:c11)
endif()

# -------- Options --------
option(D8_BUILD_TESTS     "Build unit tests" ON)
option(D8_BUILD_EXAMPLES  "Build examples" ON)
option(D8_BUILD_BENCHMARKS "Build benchmarks" ON)

# -------- Compiler flags --------
if (MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  endif()
endif()

# -------- Core Library --------
add_library(d8_core STATIC
  src/swarm.c
  src/swarm_core.c
  src/tlv.c
  src/crc32_ieee.c
  src/vsb.c
  src/udp_io.c
)

target_include_directories(d8_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# -------- Bake Generator Library --------
add_library(d8_bake_gen STATIC
  bake_gen/bake_gen.c
)

target_include_directories(d8_bake_gen PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bake_gen>
  $<INSTALL_INTERFACE:include>
)

# -------- Tests --------
if (D8_BUILD_TESTS)
  enable_testing()

  # Simple flash test
  add_executable(d8_test_flash tests/test_flash.c)
  target_link_libraries(d8_test_flash d8_core d8_bake_gen)
  target_include_directories(d8_test_flash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bake_gen)
  add_test(NAME flash_test COMMAND d8_test_flash)

  # Bake generator test
  add_executable(d8_test_bake_gen tests/test_bake_gen.c)
  target_link_libraries(d8_test_bake_gen d8_bake_gen)
  add_test(NAME bake_gen_test COMMAND d8_test_bake_gen)

  # Small swarm serialization test
  add_executable(d8_test_small_swarm tests/test_small_swarm_serialization.c)
  target_link_libraries(d8_test_small_swarm d8_core d8_bake_gen)
  target_include_directories(d8_test_small_swarm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bake_gen)
  add_test(NAME small_swarm_test COMMAND d8_test_small_swarm)
endif()

# -------- Benchmarks --------
if (D8_BUILD_BENCHMARKS)
  # Swarm benchmark
  add_executable(d8_swarm_bench bench/swarm_bench.c)
  target_link_libraries(d8_swarm_bench d8_core)

  # Swarm solver (outputs solutions)
  add_executable(d8_swarm_solver bench/swarm_solver.c)
  target_link_libraries(d8_swarm_solver d8_core)

  # Bake generator benchmark
  add_executable(d8_bake_gen_bench bench/bake_gen_bench.c)
  target_link_libraries(d8_bake_gen_bench d8_bake_gen)
endif()

# -------- Examples --------
if (D8_BUILD_EXAMPLES)
  # Simple flash example
  add_executable(d8_example_flash examples/simple_flash.c)
  target_link_libraries(d8_example_flash d8_core d8_bake_gen)
  target_include_directories(d8_example_flash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bake_gen)
  
  # Bake generator example
  add_executable(d8_example_bake_gen examples/create_bake.c)
  target_link_libraries(d8_example_bake_gen d8_bake_gen)
  target_include_directories(d8_example_bake_gen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bake_gen)
endif()

# -------- Install --------
include(GNUInstallDirs)

install(TARGETS d8_core d8_bake_gen
  EXPORT decima8-core-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY bake_gen/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bake_gen
  FILES_MATCHING PATTERN "*.h"
)

install(EXPORT decima8-core-targets
  FILE decima8-core-targets.cmake
  NAMESPACE d8::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/decima8-core
)

# -------- Print configuration --------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "Decima-8 Core v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS "C Standard:      C${CMAKE_C_STANDARD}")
message(STATUS "Build tests:     ${D8_BUILD_TESTS}")
message(STATUS "Build examples:  ${D8_BUILD_EXAMPLES}")
message(STATUS "Build benchmarks: ${D8_BUILD_BENCHMARKS}")
message(STATUS "========================================")
message(STATUS "")

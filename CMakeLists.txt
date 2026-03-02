cmake_minimum_required(VERSION 3.24)

project(decima8-core
  VERSION 0.2.0
  DESCRIPTION "Decima-8 Neuromorphic Core (Open Source)"
  LANGUAGES CXX
)

# -------- Global settings --------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Статический рантайм для MSVC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -------- Options --------
option(D8_BUILD_TESTS           "Build unit tests" ON)
option(D8_BUILD_EXAMPLES        "Build examples"   ON)
option(D8_USE_ZLIB_CRC32        "Use zlib crc32() instead of internal IEEE implementation" OFF)
option(D8_SNAPSHOT_WORLD_BEFORE "Include WORLD16(before) into d8_view_snapshot" OFF)

# -------- Global Compiler Flags --------
if (MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /D_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  endif()
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# -------- Dependencies --------
# spdlog для логирования
set(SPDLOG_ROOT "${CMAKE_SOURCE_DIR}/deps/spdlog-1.17.0")
if (NOT EXISTS "${SPDLOG_ROOT}/include/spdlog/spdlog.h")
  message(FATAL_ERROR "spdlog not found at ${SPDLOG_ROOT}. Please run: git submodule update --init --recursive")
endif()

add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE "${SPDLOG_ROOT}/include")
target_compile_definitions(spdlog INTERFACE FMT_HEADER_ONLY=1)

# -------- Library: d8_bake --------
add_library(d8_bake STATIC
  src/bake.cpp
  src/tlv.cpp
  src/crc32_ieee.cpp
)
target_include_directories(d8_bake PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(d8_bake PUBLIC spdlog)

if (D8_USE_ZLIB_CRC32)
  set(ZLIB_USE_STATIC_LIBS ON)
  find_package(ZLIB REQUIRED)
  target_link_libraries(d8_bake PUBLIC ZLIB::ZLIB)
  target_compile_definitions(d8_bake PUBLIC D8_USE_ZLIB_CRC32=1)
else()
  target_compile_definitions(d8_bake PUBLIC D8_USE_ZLIB_CRC32=0)
endif()

# -------- Library: d8_swarm --------
add_library(d8_swarm STATIC src/swarm.cpp)
target_link_libraries(d8_swarm PUBLIC d8_bake spdlog)
target_include_directories(d8_swarm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# SSE4.2 для SIMD оптимизаций
if (MSVC)
    target_compile_options(d8_swarm PRIVATE /arch:SSE2)
    option(D8_USE_AVX512 "Enable AVX512 optimizations" OFF)
    if (D8_USE_AVX512)
        target_compile_options(d8_swarm PRIVATE /arch:AVX512)
        target_compile_definitions(d8_swarm PRIVATE D8_SIMD_AVX512=1)
    endif()
else()
    target_compile_options(d8_swarm PRIVATE -msse4.2)
    option(D8_USE_AVX512 "Enable AVX512 optimizations" OFF)
    if (D8_USE_AVX512)
        target_compile_options(d8_swarm PRIVATE -mavx512f -mavx512bw)
        target_compile_definitions(d8_swarm PRIVATE D8_SIMD_AVX512=1)
    endif()
endif()

if (D8_SNAPSHOT_WORLD_BEFORE)
  target_compile_definitions(d8_swarm PUBLIC D8_SNAPSHOT_WORLD_BEFORE=1)
else()
  target_compile_definitions(d8_swarm PUBLIC D8_SNAPSHOT_WORLD_BEFORE=0)
endif()

# -------- Tests --------
if (D8_BUILD_TESTS)
  enable_testing()
  
  add_executable(d8_tests 
    tests/test_main.cpp 
    tests/test_bake.cpp 
    tests/test_swarm.cpp 
    tests/test_determinism.cpp
    tests/test_swarm_basic.cpp
  )
  target_link_libraries(d8_tests PRIVATE d8_swarm spdlog)
  add_test(NAME d8_tests COMMAND d8_tests)
endif()

# -------- Examples --------
if (D8_BUILD_EXAMPLES)
  add_executable(bake_roundtrip examples/bake_roundtrip.cpp)
  target_link_libraries(bake_roundtrip PRIVATE d8_bake spdlog)
  
  add_executable(ide_inprocess examples/ide_inprocess.cpp)
  target_link_libraries(ide_inprocess PRIVATE d8_swarm spdlog)
  
  add_executable(vsb_tape_gen examples/vsb_tape_gen.cpp)
  target_link_libraries(vsb_tape_gen PRIVATE d8_swarm spdlog)
endif()

# -------- Installation --------
install(DIRECTORY include/d8 DESTINATION include)
install(TARGETS d8_bake d8_swarm
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

# -------- Summary --------
message("")
message("========================================")
message("Decima-8 Core v${PROJECT_VERSION}")
message("========================================")
message("Build tests:     ${D8_BUILD_TESTS}")
message("Build examples:  ${D8_BUILD_EXAMPLES}")
message("Use zlib CRC32:  ${D8_USE_ZLIB_CRC32}")
message("AVX512:          ${D8_USE_AVX512}")
message("========================================")
message("")

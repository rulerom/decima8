cmake_minimum_required(VERSION 3.24)

project(decima8
  VERSION 0.1.0
  LANGUAGES CXX
)

# -------- Global settings (Ставим до объявления таргетов) --------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 1. ВКЛЮЧАЕМ СТАТИЧЕСКИЙ РАНТАЙМ ДЛЯ ВСЕГО ПРОЕКТА (/MT и /MTd)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -------- Options --------
option(D8_BUILD_TESTS           "Build unit tests" ON)
option(D8_BUILD_EXAMPLES        "Build examples"   ON)
option(D8_BUILD_IDE             "Build IDE application" OFF)
option(D8_USE_ZLIB_CRC32        "Use zlib crc32() instead of internal IEEE implementation" OFF)
option(D8_SNAPSHOT_WORLD_BEFORE "Include WORLD16(before) into d8_view_snapshot" OFF)

# -------- Global Compiler Flags --------
if (MSVC)
  # 2. Добавляем подавление варнинга u8path для C++23
  # 3. /utf-8 для поддержки Unicode в fmt/spdlog
  add_compile_options(/W4 /permissive- /utf-8 /D_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  # Make Linux builds comparable with Windows Release
  if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  endif()
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# -------- Dependencies --------
# spdlog для логирования
set(SPDLOG_ROOT "${CMAKE_SOURCE_DIR}/deps/spdlog-1.17.0")
if (NOT EXISTS "${SPDLOG_ROOT}/include/spdlog/spdlog.h")
  message(FATAL_ERROR "spdlog not found at ${SPDLOG_ROOT}")
endif()

# spdlog с bundled fmt (header-only)
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE "${SPDLOG_ROOT}/include")
# Используем bundled fmt (не внешний)
# НЕ определяем SPDLOG_FMT_EXTERNAL, чтобы использовать bundled fmt
# FMT_HEADER_ONLY=1 для header-only режима
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
  set(ZLIB_USE_STATIC_LIBS ON) # Чтобы линковать zlib тоже статически
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

# Включаем SSE4.2 для SIMD оптимизаций (базовая версия для всех процессоров)
# AVX512 версия доступна через опцию компиляции для современных машин
# ВАЖНО: используем только SSE4.2 по умолчанию, чтобы код работал на процессорах без AVX2
if (MSVC)
    # По умолчанию SSE4.2 (через /arch:SSE2, SSE4.2 intrinsics доступны через intrin.h)
    target_compile_options(d8_swarm PRIVATE /arch:SSE2)
    
    # Опционально: AVX512 для современных машин (определить D8_USE_AVX512=1 при компиляции)
    # Для включения AVX512 нужно собрать с: cmake -DD8_USE_AVX512=ON
    option(D8_USE_AVX512 "Enable AVX512 optimizations (requires AVX512-capable CPU)" OFF)
    if (D8_USE_AVX512)
        target_compile_options(d8_swarm PRIVATE /arch:AVX512)
        target_compile_definitions(d8_swarm PRIVATE D8_SIMD_AVX512=1)
    endif()
else()
    target_compile_options(d8_swarm PRIVATE -msse4.2)
    # Опционально: AVX512 для современных машин
    option(D8_USE_AVX512 "Enable AVX512 optimizations (requires AVX512-capable CPU)" OFF)
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

# -------- IDE Application --------
if (D8_BUILD_IDE)
  set(WUI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/deps/wui)
  
  # Собрать wui как subdirectory (чтобы автоматически пересобирался при изменениях)
  file(GLOB WUI_SOURCES_LIST
    ${WUI_ROOT}/src/common/*.cpp
    ${WUI_ROOT}/src/control/*.cpp
    ${WUI_ROOT}/src/config/*.cpp
    ${WUI_ROOT}/src/event/*.cpp
    ${WUI_ROOT}/src/framework/*.cpp
    ${WUI_ROOT}/src/graphic/*.cpp
    ${WUI_ROOT}/src/theme/*.cpp
    ${WUI_ROOT}/src/locale/*.cpp
    ${WUI_ROOT}/src/system/*.cpp
    ${WUI_ROOT}/src/window/*.cpp)
  
  # Исключить Linux-специфичные файлы на Windows
  if (WIN32)
    list(REMOVE_ITEM WUI_SOURCES_LIST "${WUI_ROOT}/src/system/udev_handler.cpp")
    list(REMOVE_ITEM WUI_SOURCES_LIST "${WUI_ROOT}/src/window/listener.cpp")
  endif()
  
  add_library(wui STATIC ${WUI_SOURCES_LIST})
  
  target_include_directories(wui PUBLIC
    ${WUI_ROOT}/include
    ${WUI_ROOT}/thirdparty
  )
  
  # Настройки компилятора для wui
  if (MSVC)
    target_compile_options(wui PRIVATE /W4 /permissive- /utf-8)
    # Использовать статический runtime (как и весь проект)
    set_target_properties(wui PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    # Определить UNICODE и _UNICODE для использования Unicode версий Windows API
    target_compile_definitions(wui PRIVATE UNICODE _UNICODE)
  else()
    # Linux: нужен Cairo
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
      pkg_check_modules(CAIRO QUIET cairo)
      if (CAIRO_FOUND)
        target_include_directories(wui PRIVATE ${CAIRO_INCLUDE_DIRS})
        target_link_libraries(wui PRIVATE ${CAIRO_LIBRARIES})
      endif()
    endif()
    # Linux: X11 (XOpenDisplay)
    find_package(X11 REQUIRED)
    if (X11_FOUND)
      target_link_libraries(wui PRIVATE X11::X11)
    endif()
    pkg_check_modules(XCB QUIET xcb)
    if (XCB_FOUND)
      target_link_libraries(wui PRIVATE ${XCB_LIBRARIES})
    endif()
    pkg_check_modules(XCB_IMAGE QUIET xcb-image)
    if (XCB_IMAGE_FOUND)
      target_link_libraries(wui PRIVATE ${XCB_IMAGE_LIBRARIES})
    endif()
    pkg_check_modules(XCB_CURSOR QUIET xcb-cursor)
    if (XCB_CURSOR_FOUND)
      target_link_libraries(wui PRIVATE ${XCB_CURSOR_LIBRARIES})
    endif()
    pkg_check_modules(XCB_ICCCM QUIET xcb-icccm)
    if (XCB_ICCCM_FOUND)
      target_link_libraries(wui PRIVATE ${XCB_ICCCM_LIBRARIES})
    endif()
    pkg_check_modules(X11_XCB QUIET x11-xcb)
    if (X11_XCB_FOUND)
      target_link_libraries(wui PRIVATE ${X11_XCB_LIBRARIES})
    endif()
    pkg_check_modules(UDEV QUIET libudev)
    if (UDEV_FOUND)
      target_link_libraries(wui PRIVATE ${UDEV_LIBRARIES})
    endif()
  endif()
  
  # Установить стандарт C++23 для wui (как и весь проект)
  set_target_properties(wui PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )
  
  # Создать алиас для совместимости
  add_library(wui_lib ALIAS wui)

  add_executable(d8_ide
    ide/main.cpp
    ide/MainFrame/MainFrame.cpp
    ide/Conductor/Conductor.cpp
    ide/SwarmView/SwarmView.cpp
    ide/ControlPanel/ControlPanel.cpp
    ide/TilePropertiesPanel/TilePropertiesPanel.cpp
    ide/Compass/Compass.cpp
    ide/WinnerList/WinnerList.cpp
    ide/SolutionsPanel/SolutionsPanel.cpp
    ide/TapeDeck/TapeDeck.cpp
    ide/Accordion/Accordion.cpp
    ide/Net/UdpIo.cpp
    ide/FileDialog/FileDialog.cpp
    ide/bake_gen_simple.hpp
    src/hw.rc
  )

  set_target_properties(d8_ide PROPERTIES 
    WIN32_EXECUTABLE TRUE
  )

  if (WIN32)
    target_link_directories(d8_ide PRIVATE "${WUI_ROOT}/thirdparty/boost/lib/win")
  endif()

  target_include_directories(d8_ide PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ide
    ${CMAKE_CURRENT_SOURCE_DIR}/src  # Для resource.h из hw.rc
  )
  

  # Линкуем всё вместе
  target_link_libraries(d8_ide PRIVATE
    d8_swarm
    wui
    spdlog)
  
  if (WIN32)
    target_link_libraries(d8_ide PRIVATE libboost_nowide-vc143-mt-s-x64-1_88)
    target_link_libraries(d8_ide PRIVATE comdlg32)
  endif()

  if (WIN32)
    # 4. Специфичные либы для GUI (могут понадобиться wui)
    target_link_libraries(d8_ide PRIVATE
      user32
      gdi32
      shell32
      ole32
      comctl32
      gdiplus
      setupapi)
  endif()
endif()

# -------- Tests & Examples (кратко) --------
if (D8_BUILD_TESTS)
  enable_testing()
  add_executable(d8_tests tests/test_main.cpp tests/test_bake.cpp tests/test_swarm.cpp tests/test_determinism.cpp tests/test_swarm_basic.cpp)
  target_link_libraries(d8_tests PRIVATE d8_swarm spdlog)
  add_test(NAME d8_tests COMMAND d8_tests)
endif()

if (D8_BUILD_EXAMPLES)
  add_executable(bake_roundtrip examples/bake_roundtrip.cpp)
  target_link_libraries(bake_roundtrip PRIVATE d8_bake spdlog)
  add_executable(ide_inprocess examples/ide_inprocess.cpp)
  target_link_libraries(ide_inprocess PRIVATE d8_swarm spdlog)
  add_executable(vsb_tape_gen examples/vsb_tape_gen.cpp)
  target_link_libraries(vsb_tape_gen PRIVATE d8_swarm spdlog)
  
  add_executable(bmp_to_vsb examples/bmp_to_vsb.cpp)
  target_link_libraries(bmp_to_vsb PRIVATE spdlog)
  add_executable(bake_gen examples/bake_gen.cpp)
  target_link_libraries(bake_gen PRIVATE d8_bake spdlog)
  
  add_executable(market_to_vsb examples/market_to_vsb.cpp)
  target_link_libraries(market_to_vsb PRIVATE spdlog)
  add_executable(market_vision examples/market_vision.cpp)
  target_link_libraries(market_vision PRIVATE spdlog)
  
  add_executable(cpp_to_vsb examples/cpp_to_vsb.cpp)
  target_link_libraries(cpp_to_vsb PRIVATE spdlog)
  add_executable(vsb_to_html examples/vsb_to_html.cpp)
  target_link_libraries(vsb_to_html PRIVATE spdlog)

  add_executable(text256_to_vsb examples/text256_to_vsb.cpp)
  target_link_libraries(text256_to_vsb PRIVATE spdlog)

  add_executable(text127_to_vsb examples/text127_to_vsb.cpp)
  target_link_libraries(text127_to_vsb PRIVATE spdlog)

  add_executable(sound_to_udp examples/sound_to_udp.cpp)
  target_link_libraries(sound_to_udp PRIVATE spdlog)
endif()

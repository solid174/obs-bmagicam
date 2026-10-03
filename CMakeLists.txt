cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/finders")

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swresample)

target_link_libraries(plugin-support PUBLIC OBS::libobs)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swresample
)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/filter/beautify-filter.cpp
    src/filter/beautify-filter.hpp
    src/source/camera-source.cpp
    src/source/camera-source.hpp
    src/stream/ffmpeg-check.cpp
    src/stream/ffmpeg-check.hpp
    src/stream/media-clock.cpp
    src/stream/media-clock.hpp
    src/stream/stream-receiver.cpp
    src/stream/stream-receiver.hpp
    src/ui/controls-dock.cpp
    src/ui/controls-dock.hpp
    src/ui/frontend.cpp
    src/ui/frontend.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

enable_testing()
add_subdirectory(tests)

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
find_package(MbedTLS REQUIRED)
find_package(nlohmann_json REQUIRED)

# cpp-httplib talks to the phone; only its header is used, so its own CMake, which looks for OpenSSL, stays out
include(FetchContent)
FetchContent_Declare(
  httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.59.0.tar.gz
  URL_HASH SHA256=7c8cc7df044abb837d75f7c1e56333305acb40335c3788b8f5fbf5927e63e148
  SOURCE_SUBDIR
  header-only
)
FetchContent_MakeAvailable(httplib)
add_library(httplib INTERFACE)
target_include_directories(httplib SYSTEM INTERFACE "${httplib_SOURCE_DIR}")
target_compile_definitions(
  httplib
  INTERFACE CPPHTTPLIB_MBEDTLS_SUPPORT CPPHTTPLIB_DISABLE_MACOSX_AUTOMATIC_ROOT_CERTIFICATES
)
target_link_libraries(httplib INTERFACE MbedTLS::mbedtls)

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
    httplib
    nlohmann_json::nlohmann_json
    $<$<PLATFORM_ID:Windows>:ws2_32>
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
    src/camera/camera-session.cpp
    src/camera/camera-session.hpp
    src/camera/network.cpp
    src/camera/network.hpp
    src/camera/stream-presets.cpp
    src/camera/stream-presets.hpp
    src/camera/streaming-xml.cpp
    src/camera/streaming-xml.hpp
    src/filter/beautify-filter.cpp
    src/filter/beautify-filter.hpp
    src/phone/camera-client.cpp
    src/phone/camera-client.hpp
    src/phone/event-socket.cpp
    src/phone/event-socket.hpp
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

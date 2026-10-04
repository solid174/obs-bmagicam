include(FetchContent)

FetchContent_Declare(
  doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
  URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
  SYSTEM
)
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF)
set(DOCTEST_NO_INSTALL ON)
FetchContent_MakeAvailable(doctest)

add_executable(obs-bmagicam-tests)
target_sources(
  obs-bmagicam-tests
  PRIVATE
    test-main.cpp
    lag-estimate-test.cpp
    looks-test.cpp
    media-clock-test.cpp
    streaming-test.cpp
    write-queue-test.cpp
    ../src/camera/looks.cpp
    ../src/camera/stream-presets.cpp
    ../src/camera/streaming-xml.cpp
    ../src/stream/media-clock.cpp
    ../src/phone/write-queue.cpp
    ../src/sync/lag-estimate.cpp
)
target_include_directories(obs-bmagicam-tests PRIVATE ../src)
target_compile_definitions(obs-bmagicam-tests PRIVATE LOOKS_FILE="${CMAKE_SOURCE_DIR}/data/looks/builtin.json")
target_link_libraries(obs-bmagicam-tests PRIVATE doctest::doctest nlohmann_json::nlohmann_json FFmpeg::avutil)

add_test(NAME obs-bmagicam-tests COMMAND obs-bmagicam-tests)

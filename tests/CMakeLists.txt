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
target_sources(obs-bmagicam-tests PRIVATE test-main.cpp media-clock-test.cpp ../src/stream/media-clock.cpp)
target_include_directories(obs-bmagicam-tests PRIVATE ../src)
target_link_libraries(obs-bmagicam-tests PRIVATE doctest::doctest)

add_test(NAME obs-bmagicam-tests COMMAND obs-bmagicam-tests)

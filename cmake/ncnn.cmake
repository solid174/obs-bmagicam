# ncnn runs the face tracking models of Beautify on the CPU (docs/architecture.md, "Face tracking"). It is built at
# configure time into .deps, as the template builds OBS's libraries: a static library with only the layers the models
# use, and without OpenMP, Vulkan and tools. On macOS it is built once per architecture and combined, because ncnn's
# build chooses its optimized code for one architecture only. A build is kept until the version or the options change.

include_guard(GLOBAL)

include(FetchContent)

set(_ncnn_version 20260526)
FetchContent_Declare(
  ncnn
  URL https://github.com/Tencent/ncnn/archive/refs/tags/${_ncnn_version}.tar.gz
  URL_HASH SHA256=da1ade826bc22858a9fb87ae052789bbd614d042b3ec2c22e6544ca83db6bc04
  SOURCE_SUBDIR
  not-built-here
)

# The layers in data/models, and those that ncnn creates inside: in its convolutions, and to convert, lay out,
# pad and normalize data
set(
  _ncnn_layers
  Bias
  BinaryOp
  Cast
  Clip
  Concat
  Convolution
  ConvolutionDepthWise
  Crop
  Flatten
  InnerProduct
  Input
  Interp
  Packing
  Padding
  Permute
  Pooling
  PReLU
  ReLU
  Reshape
  Scale
  Sigmoid
  Split
)

set(
  _ncnn_options
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_LIBDIR=lib
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DNCNN_OPENMP=OFF
  -DNCNN_VULKAN=OFF
  -DNCNN_STDIO=OFF
  -DNCNN_C_API=OFF
  -DNCNN_INT8=OFF
  -DNCNN_BF16=OFF
  -DNCNN_PIXEL_ROTATE=OFF
  -DNCNN_PIXEL_DRAWING=OFF
  -DNCNN_BUILD_TOOLS=OFF
  -DNCNN_BUILD_EXAMPLES=OFF
  -DNCNN_BUILD_BENCHMARK=OFF
  -DNCNN_BUILD_TESTS=OFF
  -DNCNN_PYTHON=OFF
)
# Intel Macs get ncnn's AVX2 code; the few with AVX-512 are not worth building its AVX-512 code for each time
set(
  _ncnn_macos_x86_64
  -DNCNN_AVX512=OFF
  -DNCNN_AVX512VNNI=OFF
  -DNCNN_AVX512BF16=OFF
  -DNCNN_AVX512FP16=OFF
  -DNCNN_AVXVNNI=OFF
  -DNCNN_AVXVNNIINT8=OFF
  -DNCNN_AVXVNNIINT16=OFF
  -DNCNN_AVXNECONVERT=OFF
)
string(SHA256 _ncnn_stamp "${_ncnn_version};${_ncnn_layers};${_ncnn_options};${_ncnn_macos_x86_64}")
set(_ncnn_root "${CMAKE_CURRENT_SOURCE_DIR}/.deps/ncnn-${_ncnn_version}")

# Builds ncnn for one architecture into .deps/ncnn-<version>/<architecture>, unless it is built there already
function(_build_ncnn architecture)
  set(_build "${_ncnn_root}/build-${architecture}")
  set(_install "${_ncnn_root}/${architecture}")
  if(EXISTS "${_install}/${_ncnn_stamp}")
    return()
  endif()
  FetchContent_MakeAvailable(ncnn)
  set(_options ${_ncnn_options})
  file(STRINGS "${ncnn_SOURCE_DIR}/src/CMakeLists.txt" _lines REGEX "^ncnn_add_layer\\(")
  foreach(_line IN LISTS _lines)
    string(REGEX REPLACE "^ncnn_add_layer\\(([A-Za-z0-9_]+).*" "\\1" _layer "${_line}")
    string(TOLOWER "${_layer}" _name)
    if(_layer IN_LIST _ncnn_layers)
      list(APPEND _options -DWITH_LAYER_${_name}=ON)
    else()
      list(APPEND _options -DWITH_LAYER_${_name}=OFF)
    endif()
  endforeach()
  find_program(_ccache ccache NO_CACHE)
  if(_ccache)
    list(APPEND _options -DCMAKE_C_COMPILER_LAUNCHER=${_ccache} -DCMAKE_CXX_COMPILER_LAUNCHER=${_ccache})
  endif()
  if(OS_MACOS)
    list(
      APPEND
      _options
      -G
      "Unix Makefiles"
      -DCMAKE_OSX_ARCHITECTURES=${architecture}
      -DCMAKE_SYSTEM_PROCESSOR=${architecture}
      -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
    )
    if(architecture STREQUAL "x86_64")
      list(APPEND _options ${_ncnn_macos_x86_64})
    endif()
  elseif(OS_WINDOWS)
    list(APPEND _options -G "${CMAKE_GENERATOR}" -A "${CMAKE_GENERATOR_PLATFORM}")
  else()
    list(
      APPEND
      _options
      -G
      "${CMAKE_GENERATOR}"
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    )
  endif()
  message(STATUS "Build ncnn ${_ncnn_version} (${architecture})")
  file(REMOVE_RECURSE "${_build}" "${_install}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${ncnn_SOURCE_DIR}" -B "${_build}" ${_options} "-DCMAKE_INSTALL_PREFIX=${_install}"
    OUTPUT_QUIET
    COMMAND_ERROR_IS_FATAL ANY
  )
  # The compiler's many warnings about ncnn's code are shown only when its build fails
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build}" --config Release --parallel --target install
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output
    RESULT_VARIABLE _result
  )
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Building ncnn failed:\n${_output}")
  endif()
  file(REMOVE_RECURSE "${_build}")
  file(TOUCH "${_install}/${_ncnn_stamp}")
  message(STATUS "Build ncnn ${_ncnn_version} (${architecture}) - done")
endfunction()

if(OS_MACOS)
  _build_ncnn(arm64)
  _build_ncnn(x86_64)
  # One library with both architectures; the headers differ only in platform.h, which each architecture gets its own
  set(_ncnn_dir "${_ncnn_root}/universal")
  set(_ncnn_library "${_ncnn_dir}/lib/libncnn.a")
  if(NOT EXISTS "${_ncnn_dir}/${_ncnn_stamp}")
    file(REMOVE_RECURSE "${_ncnn_dir}")
    file(MAKE_DIRECTORY "${_ncnn_dir}/lib")
    execute_process(
      COMMAND
        lipo -create "${_ncnn_root}/arm64/lib/libncnn.a" "${_ncnn_root}/x86_64/lib/libncnn.a" -output "${_ncnn_library}"
      COMMAND_ERROR_IS_FATAL ANY
    )
    file(COPY "${_ncnn_root}/arm64/include" DESTINATION "${_ncnn_dir}")
    file(RENAME "${_ncnn_dir}/include/ncnn/platform.h" "${_ncnn_dir}/include/ncnn/platform-arm64.h")
    file(COPY_FILE "${_ncnn_root}/x86_64/include/ncnn/platform.h" "${_ncnn_dir}/include/ncnn/platform-x86_64.h")
    file(
      WRITE
      "${_ncnn_dir}/include/ncnn/platform.h"
      "#if defined(__aarch64__)\n#include \"platform-arm64.h\"\n#else\n#include \"platform-x86_64.h\"\n#endif\n"
    )
    file(TOUCH "${_ncnn_dir}/${_ncnn_stamp}")
  endif()
else()
  _build_ncnn(${CMAKE_SYSTEM_PROCESSOR})
  set(_ncnn_dir "${_ncnn_root}/${CMAKE_SYSTEM_PROCESSOR}")
  if(OS_WINDOWS)
    set(_ncnn_library "${_ncnn_dir}/lib/ncnn.lib")
  else()
    set(_ncnn_library "${_ncnn_dir}/lib/libncnn.a")
  endif()
endif()

find_package(Threads REQUIRED)
add_library(ncnn STATIC IMPORTED GLOBAL)
set_target_properties(
  ncnn
  PROPERTIES
    IMPORTED_LOCATION "${_ncnn_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${_ncnn_dir}/include/ncnn"
    INTERFACE_LINK_LIBRARIES Threads::Threads
    # ncnn's headers include windows.h, whose min and max macros would break std::min and std::max
    INTERFACE_COMPILE_DEFINITIONS $<$<PLATFORM_ID:Windows>:NOMINMAX>
)

# Finds the FFmpeg libraries that OBS uses: obs-deps on Windows and macOS, the system's on Linux.
#
# Components are FFmpeg library names without the "lib" prefix, such as avcodec. Each component found gets an
# imported target FFmpeg::<component>.

include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)

foreach(component IN LISTS FFmpeg_FIND_COMPONENTS)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_FFmpeg_${component} QUIET lib${component})
  endif()

  find_path(
    FFmpeg_${component}_INCLUDE_DIR
    NAMES lib${component}/${component}.h
    HINTS ${PC_FFmpeg_${component}_INCLUDE_DIRS}
  )
  find_library(
    FFmpeg_${component}_LIBRARY
    NAMES ${component} lib${component}
    HINTS ${PC_FFmpeg_${component}_LIBRARY_DIRS}
  )
  mark_as_advanced(FFmpeg_${component}_INCLUDE_DIR FFmpeg_${component}_LIBRARY)

  if(FFmpeg_${component}_INCLUDE_DIR AND FFmpeg_${component}_LIBRARY)
    set(FFmpeg_${component}_FOUND TRUE)
    if(NOT TARGET FFmpeg::${component})
      add_library(FFmpeg::${component} UNKNOWN IMPORTED)
      set_target_properties(
        FFmpeg::${component}
        PROPERTIES
          IMPORTED_LOCATION "${FFmpeg_${component}_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_${component}_INCLUDE_DIR}"
      )
    endif()
  endif()
endforeach()

find_package_handle_standard_args(FFmpeg HANDLE_COMPONENTS)

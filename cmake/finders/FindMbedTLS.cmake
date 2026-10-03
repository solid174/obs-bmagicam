# Finds the Mbed TLS libraries that OBS uses: obs-deps on Windows (static) and macOS (shared), the system's on Linux.
#
# Imported targets: MbedTLS::mbedtls, which links MbedTLS::mbedx509 and MbedTLS::mbedcrypto.

include(FindPackageHandleStandardArgs)

find_path(MbedTLS_INCLUDE_DIR NAMES mbedtls/ssl.h)
mark_as_advanced(MbedTLS_INCLUDE_DIR)

foreach(library IN ITEMS mbedtls mbedx509 mbedcrypto)
  find_library(MbedTLS_${library}_LIBRARY NAMES ${library})
  mark_as_advanced(MbedTLS_${library}_LIBRARY)
endforeach()

find_package_handle_standard_args(
  MbedTLS
  REQUIRED_VARS MbedTLS_INCLUDE_DIR MbedTLS_mbedtls_LIBRARY MbedTLS_mbedx509_LIBRARY MbedTLS_mbedcrypto_LIBRARY
)

if(MbedTLS_FOUND AND NOT TARGET MbedTLS::mbedtls)
  add_library(MbedTLS::mbedcrypto UNKNOWN IMPORTED)
  set_target_properties(
    MbedTLS::mbedcrypto
    PROPERTIES IMPORTED_LOCATION "${MbedTLS_mbedcrypto_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${MbedTLS_INCLUDE_DIR}"
  )
  if(WIN32)
    # The random number generator of the static library uses the system's
    set_property(TARGET MbedTLS::mbedcrypto PROPERTY INTERFACE_LINK_LIBRARIES bcrypt)
  endif()

  add_library(MbedTLS::mbedx509 UNKNOWN IMPORTED)
  set_target_properties(
    MbedTLS::mbedx509
    PROPERTIES IMPORTED_LOCATION "${MbedTLS_mbedx509_LIBRARY}" INTERFACE_LINK_LIBRARIES MbedTLS::mbedcrypto
  )

  add_library(MbedTLS::mbedtls UNKNOWN IMPORTED)
  set_target_properties(
    MbedTLS::mbedtls
    PROPERTIES IMPORTED_LOCATION "${MbedTLS_mbedtls_LIBRARY}" INTERFACE_LINK_LIBRARIES MbedTLS::mbedx509
  )
endif()

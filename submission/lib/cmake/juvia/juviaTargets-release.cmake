#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "juvia::juvia" for configuration "Release"
set_property(TARGET juvia::juvia APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(juvia::juvia PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libjuvia.so"
  IMPORTED_SONAME_RELEASE "libjuvia.so"
  )

list(APPEND _cmake_import_check_targets juvia::juvia )
list(APPEND _cmake_import_check_files_for_juvia::juvia "${_IMPORT_PREFIX}/lib/libjuvia.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

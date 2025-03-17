#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "utf8_range::utf8_validity" for configuration "Release"
set_property(TARGET utf8_range::utf8_validity APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(utf8_range::utf8_validity PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib64/libutf8_validity.so"
  IMPORTED_SONAME_RELEASE "libutf8_validity.so"
  )

list(APPEND _cmake_import_check_targets utf8_range::utf8_validity )
list(APPEND _cmake_import_check_files_for_utf8_range::utf8_validity "${_IMPORT_PREFIX}/lib64/libutf8_validity.so" )

# Import target "utf8_range::utf8_range" for configuration "Release"
set_property(TARGET utf8_range::utf8_range APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(utf8_range::utf8_range PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib64/libutf8_range.so"
  IMPORTED_SONAME_RELEASE "libutf8_range.so"
  )

list(APPEND _cmake_import_check_targets utf8_range::utf8_range )
list(APPEND _cmake_import_check_files_for_utf8_range::utf8_range "${_IMPORT_PREFIX}/lib64/libutf8_range.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

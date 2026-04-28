find_path(Leptonica_INCLUDE_DIR
  NAMES leptonica/allheaders.h allheaders.h
  HINTS
    /usr/include
    /usr/local/include
)

find_library(Leptonica_LIBRARY
  NAMES leptonica lept
  HINTS
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Leptonica
  REQUIRED_VARS Leptonica_LIBRARY Leptonica_INCLUDE_DIR
)

if(Leptonica_FOUND)
  set(Leptonica_LIBRARIES ${Leptonica_LIBRARY})
  set(Leptonica_INCLUDE_DIRS ${Leptonica_INCLUDE_DIR})
endif()

mark_as_advanced(Leptonica_INCLUDE_DIR Leptonica_LIBRARY)
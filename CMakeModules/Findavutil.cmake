find_path(Libavutil_INCLUDE_DIR
  NAMES libavutil/avutil.h avutil.h
  HINTS
    /usr/include
    /usr/local/include
)

find_library(Libavutil_LIBRARY
  NAMES avutil libavutil
  HINTS
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(avutil
  REQUIRED_VARS Libavutil_LIBRARY Libavutil_INCLUDE_DIR
)

if(avutil_FOUND)
  set(Libavutil_LIBRARIES ${Libavutil_LIBRARY})
  set(Libavutil_INCLUDE_DIRS ${Libavutil_INCLUDE_DIR})
endif()

mark_as_advanced(Libavutil_INCLUDE_DIR Libavutil_LIBRARY)
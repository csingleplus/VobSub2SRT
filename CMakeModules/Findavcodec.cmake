find_path(Libavcodec_INCLUDE_DIR
  NAMES libavcodec/avcodec.h avcodec.h
  HINTS
    /usr/include
    /usr/local/include
)

find_library(Libavcodec_LIBRARY
  NAMES avcodec libavcodec
  HINTS
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(avcodec
  REQUIRED_VARS Libavcodec_LIBRARY Libavcodec_INCLUDE_DIR
)

if(avcodec_FOUND)
  set(Libavcodec_LIBRARIES ${Libavcodec_LIBRARY})
  set(Libavcodec_INCLUDE_DIRS ${Libavcodec_INCLUDE_DIR})
endif()

mark_as_advanced(Libavcodec_INCLUDE_DIR Libavcodec_LIBRARY)
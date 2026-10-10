find_path(Libavformat_INCLUDE_DIR
  NAMES libavformat/avformat.h avformat.h
  HINTS
    /usr/include
    /usr/local/include
)

find_library(Libavformat_LIBRARY
  NAMES avformat libavformat
  HINTS
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(avformat
  REQUIRED_VARS Libavformat_LIBRARY Libavformat_INCLUDE_DIR
)

if(avformat_FOUND)
  set(Libavformat_LIBRARIES ${Libavformat_LIBRARY})
  set(Libavformat_INCLUDE_DIRS ${Libavformat_INCLUDE_DIR})
endif()

mark_as_advanced(Libavformat_INCLUDE_DIR Libavformat_LIBRARY)

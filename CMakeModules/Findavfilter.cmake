find_path(Libavfilter_INCLUDE_DIR
  NAMES libavfilter/avfilter.h avfilter.h
  HINTS
    /usr/include
    /usr/local/include
)

find_library(Libavfilter_LIBRARY
  NAMES avfilter libavfilter
  HINTS
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(avfilter
  REQUIRED_VARS Libavfilter_LIBRARY Libavfilter_INCLUDE_DIR
)

if(avfilter_FOUND)
  set(Libavfilter_LIBRARIES ${Libavfilter_LIBRARY})
  set(Libavfilter_INCLUDE_DIRS ${Libavfilter_INCLUDE_DIR})
endif()

mark_as_advanced(Libavfilter_INCLUDE_DIRS Libavfilter_LIBRARIES)
set(vobsub2srt_sources
  vobsub2srt.c++
  langcodes.h++
  langcodes.c++
  cmd_options.h++
  cmd_options.c++
  time.cpp
  time.hpp
  codec.cpp
  codec.hpp
  filter_ocr.cpp
  filter_ocr.hpp)

add_executable(vobsub2srt ${vobsub2srt_sources})
if(BUILD_STATIC)
  set_target_properties(vobsub2srt PROPERTIES
                        LINK_SEARCH_END_STATIC ON
                        LINK_FLAGS -static)
endif()
target_link_libraries(vobsub2srt
				PkgConfig::AV
				PkgConfig::TESS
				PkgConfig::LEPT
				PkgConfig::PNG)

install(TARGETS vobsub2srt RUNTIME DESTINATION ${INSTALL_EXECUTABLES_PATH})

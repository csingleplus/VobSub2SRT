link_directories($(Libavfilter_LIBRARY_DIRS) ${Libavformat_LIBRARY_DIRS} ${Libavcodec_LIBRARY_DIRS} ${Libavutil_LIBRARY_DIRS})
include_directories($(Libavfilter_INCLUDE_DIRS) ${Libavformat_INCLUDE_DIRS} ${Libavcodec_INCLUDE_DIRS} ${Libavutil_INCLUDE_DIRS})

set(vobsub2srt_sources
  vobsub2srt.c++
  langcodes.h++
  langcodes.c++
  cmd_options.h++
  cmd_options.c++)

add_executable(vobsub2srt ${vobsub2srt_sources})
if(BUILD_STATIC)
  set_target_properties(vobsub2srt PROPERTIES
                        LINK_SEARCH_END_STATIC ON
                        LINK_FLAGS -static)
endif()
target_link_libraries(vobsub2srt PNG::PNG
				 ${Libavfilter_LIBRARIES}
				 ${Libavformat_LIBRARIES}
				 ${Libavcodec_LIBRARIES}
				 ${Libavutil_LIBRARIES}
				 ${Leptonica_LIBRARIES}
				 ${Tesseract_LIBRARIES})

install(TARGETS vobsub2srt RUNTIME DESTINATION ${INSTALL_EXECUTABLES_PATH})

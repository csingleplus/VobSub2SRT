include_directories(${vobsub2srt_SOURCE_DIR}/mplayer)

link_directories(${Libavutil_LIBRARY_DIRS})
include_directories(${Libavutil_INCLUDE_DIRS})
include_directories(${Tesseract_INCLUDE_DIR})
include_directories(${OpenCV_INCLUDE_DIRS})

set(vobsub2srt_sources
  vobsub2srt.c++
  langcodes.h++
  langcodes.c++
  cmd_options.h++
  cmd_options.c++
  generic_exception.h++
  debug.h++
  debug.c++
  bbox.h++
  bbox.c++
  stats.h++
  stats.c++
  OCRSymbol.h++
  OCRSymbol.c++
  OCRWord.h++
  OCRWord.c++
  OCRLine.h++
  OCRLine.c++
  OCRSubtitle.h++
  OCRSubtitle.c++
  OCRSubtitles.h++
  OCRSubtitles.c++
  Replacements.h++
  Replacements.c++
  VobSub.h++
  VobSub.c++)

add_executable(vobsub2srt ${vobsub2srt_sources})
if(BUILD_STATIC)
  set_target_properties(vobsub2srt PROPERTIES
                        LINK_SEARCH_END_STATIC ON
                        LINK_FLAGS -static)
endif()
target_link_libraries(vobsub2srt mplayer ${Libavutil_LIBRARIES} ${Leptonica_LIBRARIES} ${Tesseract_LIBRARIES} ${OpenCV_LIBS} ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS vobsub2srt RUNTIME DESTINATION ${INSTALL_EXECUTABLES_PATH})

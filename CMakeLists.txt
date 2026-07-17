cmake_minimum_required(VERSION 3.25)

project(HevcVideoExtension VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "HevcVideoExtension must be built on Windows with the Windows SDK.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FFMPEG_ROOT "" CACHE PATH "Root of an FFmpeg shared/development build")

find_path(FFMPEG_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  HINTS "${FFMPEG_ROOT}" ENV FFMPEG_ROOT
  PATH_SUFFIXES include
  REQUIRED
)

find_library(AVCODEC_LIBRARY NAMES avcodec avcodec-61 avcodec-60
  HINTS "${FFMPEG_ROOT}" ENV FFMPEG_ROOT PATH_SUFFIXES lib REQUIRED)
find_library(AVUTIL_LIBRARY NAMES avutil avutil-59 avutil-58
  HINTS "${FFMPEG_ROOT}" ENV FFMPEG_ROOT PATH_SUFFIXES lib REQUIRED)
find_library(SWSCALE_LIBRARY NAMES swscale swscale-8 swscale-7 swscale-6
  HINTS "${FFMPEG_ROOT}" ENV FFMPEG_ROOT PATH_SUFFIXES lib REQUIRED)

add_library(HevcVideoExtension SHARED
  src/DllMain.cpp
  src/HevcDecoder.cpp
  src/HevcDecoder.h
  src/Guids.cpp
  src/Guids.h
)

target_include_directories(HevcVideoExtension PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_compile_definitions(HevcVideoExtension PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
)
target_compile_options(HevcVideoExtension PRIVATE /W4 /permissive-)
target_link_libraries(HevcVideoExtension PRIVATE
  mf.lib
  mfplat.lib
  mfuuid.lib
  ole32.lib
  advapi32.lib
  "${AVCODEC_LIBRARY}"
  "${AVUTIL_LIBRARY}"
  "${SWSCALE_LIBRARY}"
)
set_target_properties(HevcVideoExtension PROPERTIES OUTPUT_NAME "HevcVideoExtension")

add_executable(HevcMftProbe tools/HevcMftProbe.cpp)
target_compile_definitions(HevcMftProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(HevcMftProbe PRIVATE /W4 /permissive-)
target_link_libraries(HevcMftProbe PRIVATE mf.lib mfplat.lib mfuuid.lib ole32.lib)

install(TARGETS HevcVideoExtension HevcMftProbe
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION bin
  ARCHIVE DESTINATION lib
)
install(FILES
  scripts/Register-HEVC.ps1
  scripts/Test-HEVC.ps1
  DESTINATION scripts
)

set(FFMPEG_URL "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip")
set(FFMPEG_PACKAGE_NAME "ffmpeg-master-latest-win64-gpl-shared")
set(FFMPEG_CACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/ffmpeg" CACHE PATH "Directory used to cache the FFmpeg archive and extracted files")
set(FFMPEG_ARCHIVE "${FFMPEG_CACHE_DIR}/${FFMPEG_PACKAGE_NAME}.zip")
set(FFMPEG_DIR "${FFMPEG_CACHE_DIR}/${FFMPEG_PACKAGE_NAME}")

file(MAKE_DIRECTORY "${FFMPEG_CACHE_DIR}")

if(NOT EXISTS ${FFMPEG_DIR})
	message(STATUS "Downloading FFmpeg...")
	file(DOWNLOAD ${FFMPEG_URL} ${FFMPEG_ARCHIVE} SHOW_PROGRESS)
	file(ARCHIVE_EXTRACT INPUT ${FFMPEG_ARCHIVE} DESTINATION "${FFMPEG_CACHE_DIR}")
endif()

set(FFMPEG_LIBS
	avcodec
	avdevice
	avfilter
	avformat
	avutil
	swscale
	swresample
)

set(FFMPEG_BIN_DIR ${FFMPEG_DIR}/bin)
set(FFMPEG_LIB_DIR ${FFMPEG_DIR}/lib)
set(FFMPEG_INCLUDE_DIR ${FFMPEG_DIR}/include)

link_directories(${FFMPEG_LIB_DIR})
include_directories(${FFMPEG_INCLUDE_DIR})

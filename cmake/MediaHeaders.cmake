# Public FFmpeg headers only; no codec is built or linked here. The decoder
# resolves the same LGPL FFmpeg 7 ABI already deployed by Qt 6.8 Multimedia.
# Keep header/runtime ABI checks explicit instead of using Qt private classes.
include(FetchContent)
FetchContent_Declare(mds_media_headers
    URL https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz
    URL_HASH SHA256=733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(mds_media_headers)
include(TestBigEndian)
test_big_endian(mds_media_big_endian)
if(mds_media_big_endian)
    set(mds_media_endian 1)
else()
    set(mds_media_endian 0)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/media-include/libavutil")
# Never add the source root itself: its VERSION file shadows C++ <version>
# on case-insensitive hosts. Expose only the four public header directories.
foreach(mds_media_lib libavutil libavcodec libavformat libswscale)
    file(GLOB mds_media_public "${mds_media_headers_SOURCE_DIR}/${mds_media_lib}/*.h")
    file(COPY ${mds_media_public} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/media-include/${mds_media_lib}")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/media-include/libavutil/avconfig.h"
    "#pragma once\n#define AV_HAVE_BIGENDIAN ${mds_media_endian}\n#define AV_HAVE_FAST_UNALIGNED 0\n")
target_include_directories(mikudancestudio_frontend_qt SYSTEM PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/media-include")

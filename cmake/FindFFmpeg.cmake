# FindFFmpeg.cmake - Find FFmpeg libraries and their dependencies (Cross-platform)

if(NOT DEFINED FFMPEG_ROOT)
    set(FFMPEG_ROOT "" CACHE PATH "Root directory of FFmpeg installation")
endif()

# Search paths
set(_ffmpeg_hint_paths
    "${FFMPEG_ROOT}"
)

if(UNIX)
    list(APPEND _ffmpeg_hint_paths
        "/usr/local"
        "/usr"
    )
endif()

# --- Find include dir ---
find_path(FFMPEG_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES include
)

# --- Platform-specific library naming ---
if(WIN32)
    # Windows: prefer import libraries (.lib) for DLLs
    set(_lib_names_avcodec avcodec)
    set(_lib_names_avformat avformat)
    set(_lib_names_avutil avutil)
    set(_lib_names_swscale swscale)
    set(_lib_names_avdevice avdevice)
    set(_lib_names_swresample swresample)
    set(_lib_names_avfilter avfilter)
    set(_lib_names_z zlib z)
    set(_lib_names_x264 libx264 x264)
else()
    # Unix: prefer static libraries
    set(_lib_names_avcodec libavcodec.a avcodec)
    set(_lib_names_avformat libavformat.a avformat)
    set(_lib_names_avutil libavutil.a avutil)
    set(_lib_names_swscale libswscale.a swscale)
    set(_lib_names_avdevice libavdevice.a avdevice)
    set(_lib_names_swresample libswresample.a swresample)
    set(_lib_names_avfilter libavfilter.a avfilter)
    set(_lib_names_z libz.a z)
    set(_lib_names_x264 libx264.a x264)
endif()

# --- Find FFmpeg libraries ---
find_library(FFMPEG_LIB_AVCODEC
    NAMES ${_lib_names_avcodec}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

find_library(FFMPEG_LIB_AVFORMAT
    NAMES ${_lib_names_avformat}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

find_library(FFMPEG_LIB_AVUTIL
    NAMES ${_lib_names_avutil}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

find_library(FFMPEG_LIB_SWSCALE
    NAMES ${_lib_names_swscale}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

find_library(FFMPEG_LIB_AVDEVICE
    NAMES ${_lib_names_avdevice}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

find_library(FFMPEG_LIB_SWRESAMPLE
    NAMES ${_lib_names_swresample}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

find_library(FFMPEG_LIB_AVFILTER
    NAMES ${_lib_names_avfilter}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

# --- Find FFmpeg dependencies (for static builds) ---

find_library(ZLIB_LIBRARY
    NAMES ${_lib_names_z}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

# x264 (optional - may not be needed on Windows shared builds)
find_library(X264_LIBRARY
    NAMES ${_lib_names_x264}
    HINTS ${_ffmpeg_hint_paths}
    PATH_SUFFIXES lib lib64
)

if(X264_LIBRARY)
    message(STATUS "Found x264: ${X264_LIBRARY}")
else()
    message(STATUS "x264 library not found (may not be required for shared builds)")
endif()

# FFmpeg libraries in dependency order
set(FFMPEG_LIBRARIES
    ${FFMPEG_LIB_AVDEVICE}
    ${FFMPEG_LIB_AVFILTER}
    ${FFMPEG_LIB_AVFORMAT}
    ${FFMPEG_LIB_AVCODEC}
    ${FFMPEG_LIB_SWRESAMPLE}
    ${FFMPEG_LIB_SWSCALE}
    ${FFMPEG_LIB_AVUTIL}
)

# Add dependencies for static builds
set(FFMPEG_DEPENDENCIES "")

if(ZLIB_LIBRARY)
    list(APPEND FFMPEG_DEPENDENCIES ${ZLIB_LIBRARY})
endif()

if(X264_LIBRARY)
    list(APPEND FFMPEG_DEPENDENCIES ${X264_LIBRARY})
endif()

# Windows-specific system libraries for FFmpeg
if(WIN32)
    list(APPEND FFMPEG_DEPENDENCIES
        ws2_32      # Windows sockets
        secur32     # Security functions
        bcrypt      # Cryptography
        mfplat      # Media Foundation
        mfuuid      # Media Foundation UUIDs
        strmiids    # DirectShow
    )
endif()

# --- Create imported target ---
if(FFMPEG_INCLUDE_DIR AND FFMPEG_LIBRARIES)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    target_include_directories(FFmpeg::FFmpeg INTERFACE "${FFMPEG_INCLUDE_DIR}")
    
    # Platform-specific linking approach
    if(WIN32)
        # Windows: no special link groups needed
        target_link_libraries(FFmpeg::FFmpeg INTERFACE 
            ${FFMPEG_LIBRARIES}
            ${FFMPEG_DEPENDENCIES}
        )
    else()
        # Unix/Linux: use link groups for circular dependencies
        target_link_libraries(FFmpeg::FFmpeg INTERFACE 
            -Wl,--start-group
            ${FFMPEG_LIBRARIES}
            ${FFMPEG_DEPENDENCIES}
            -Wl,--end-group
        )
    endif()
    
    set(FFMPEG_FOUND TRUE)
    
    # Print what we found
    if(NOT FFmpeg_FIND_QUIETLY)
        message(STATUS "Found FFmpeg:")
        message(STATUS "  Include: ${FFMPEG_INCLUDE_DIR}")
        message(STATUS "  Libraries: ${FFMPEG_LIBRARIES}")
        if(FFMPEG_DEPENDENCIES)
            message(STATUS "  Dependencies: ${FFMPEG_DEPENDENCIES}")
        endif()
    endif()
endif()

# --- Report ---
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFMPEG_INCLUDE_DIR FFMPEG_LIBRARIES
    FAIL_MESSAGE "FFmpeg not found. Set FFMPEG_ROOT to your FFmpeg installation root."
)

mark_as_advanced(
    FFMPEG_INCLUDE_DIR
    FFMPEG_LIB_AVCODEC
    FFMPEG_LIB_AVFORMAT
    FFMPEG_LIB_AVUTIL
    FFMPEG_LIB_SWSCALE
    FFMPEG_LIB_AVDEVICE
    FFMPEG_LIB_SWRESAMPLE
    FFMPEG_LIB_AVFILTER
    ZLIB_LIBRARY
    X264_LIBRARY
)
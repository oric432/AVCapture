# =========================
# RtAudio
# =========================
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(UNIX AND NOT APPLE)
    set(RTAUDIO_API_ALSA OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_JACK OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_OSS  OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_PULSE ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    rtaudio
    URL "https://github.com/thestk/rtaudio/archive/refs/tags/6.0.1.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP NO
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(rtaudio)

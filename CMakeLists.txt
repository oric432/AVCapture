cmake_minimum_required(VERSION 3.21)
project(AVCapture VERSION 0.1.0 LANGUAGES C CXX)
cmake_policy(SET CMP0069 NEW)
cmake_policy(SET CMP0135 NEW)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_TLS_VERIFY OFF)

# run clang-format on format target
include(cmake/clang-format.cmake)
add_clang_format_target(format ${CMAKE_SOURCE_DIR}/src)

# Prefer static if available 
set(BUILD_SHARED_LIBS OFF)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(UNIX AND NOT APPLE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_INSTALL_RPATH "")
endif()

# Utils
include(cmake/PreventInSourceBuilds.cmake)
# include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

myproject_assure_out_of_source_builds()

# Third-party libs you already vendor
include(third_party/spdlog.cmake)
include(third_party/boost.cmake)
include(third_party/tomlplusplus.cmake)
include(third_party/rtaudio.cmake)

# ---------------- FFmpeg ----------------
# Tell CMake where to find custom modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Hint path where you installed FFmpeg
set(FFMPEG_ROOT "/usr/local" CACHE PATH "FFmpeg installation root")
find_package(FFmpeg REQUIRED)

# --------------- Sources ----------------
set(COMMON_SOURCES
    # app
    src/main.cpp
    src/Settings.cpp

    # core
    src/core/MediaRecorder.cpp
    src/core/RollingSegments.cpp

    # api
    src/api/HttpSession.cpp
    src/api/ApiServer.cpp
    src/api/Router.cpp

    # video
    src/core/video/ScreenRecorderFactory.cpp
    src/core/video/VideoEncoder.cpp
    src/core/video/VideoEncoderQueue.cpp
    src/core/video/ScreenRecorderBase.cpp

    # audio
    src/core/audio/AudioCapturer.cpp
    src/core/audio/AudioEncoder.cpp
    src/core/audio/AudioMixer.cpp
)

set(PLATFORM_SOURCES "")
if(WIN32)
    list(APPEND PLATFORM_SOURCES
        src/platform/ScreenRecorderWindows.cpp
        src/platform/WinSessionPowerMonitor.cpp
    )
elseif(UNIX AND NOT APPLE)
    list(APPEND PLATFORM_SOURCES
        src/platform/ScreenRecorderLinux.cpp
    )
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

add_executable(AVCapture
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

# Utils
include(cmake/PreventInSourceBuilds.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

myproject_assure_out_of_source_builds()

# Define NDEBUG for debug (for asserts)
target_compile_definitions(AVCapture PRIVATE $<$<CONFIG:Release>:NDEBUG>)

# Define pch
target_precompile_headers(AVCapture PRIVATE src/pch.h)

# Warnings
myproject_set_project_warnings(
    AVCapture
    OFF
    ""
    ""
    ""
    ""
)

# Include paths
target_include_directories(AVCapture
    SYSTEM PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link
target_link_libraries(AVCapture
    PRIVATE
    rtaudio
    tomlplusplus::tomlplusplus
    spdlog::spdlog_header_only
    Boost::asio
    Boost::beast
    Boost::system
    Boost::json
    Boost::uuid
)

# Platform graphics libs
if(WIN32)
    target_link_libraries(AVCapture PRIVATE
        d3d11
        dxgi
        FFmpeg::FFmpeg
    )
elseif(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED COMPONENTS X11 Xext Xfixes)

    # X11 libs -> FFmpeg libs -> x264 -> zlib -> system libs (order matters)
    target_link_libraries(AVCapture PRIVATE
        X11::X11
        X11::Xext
        X11::Xfixes
        X11::Xrandr
        -Wl,--start-group
        FFmpeg::FFmpeg
        ${X264_LIBRARY}
        -Wl,--end-group
        z
        pthread
        m
        dl
    )
endif()

# Copy DLLs for Windows shared FFmpeg builds
if(WIN32 AND FFMPEG_ROOT)
    # Check if bin directory exists (shared build)
    if(EXISTS "${FFMPEG_ROOT}/bin")
        add_custom_command(TARGET AVCapture POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FFMPEG_ROOT}/bin"
            $<TARGET_FILE_DIR:AVCapture>
            COMMENT "Copying FFmpeg DLLs to output directory"
        )
    endif()
endif()

if(MSVC)
    target_compile_options(AVCapture PRIVATE "/W4" "/WX")
    target_compile_definitions(AVCapture PRIVATE "_CRT_SECURE_NO_WARNINGS")
else()
    # Enable Asan and Ubsan sanitizers for debug builds.
    if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        myproject_enable_sanitizers(AVCapture
            ON
            OFF
            ON
            OFF
            OFF)
    endif()

    target_compile_options(AVCapture PRIVATE -Wall -Wextra -pedantic)
endif()

install(TARGETS AVCapture RUNTIME DESTINATION bin)

# Screen Recorder C++

A high-performance, cross-platform screen recording library with circular buffer support and REST API control.

## Features

- **Cross-Platform**: Works on Windows (DirectX) and Linux (X11)
- **Hardware Acceleration**: Supports hardware-accelerated video encoding
- **Circular Buffer**: Continuously records with configurable buffer duration
- **REST API**: Control recording via HTTP endpoints
- **Real-time Statistics**: Monitor frames captured, encoded, dropped, and FPS
- **Multi-Monitor Support**: Choose which monitor to record
- **Graceful Shutdown**: Handles Ctrl+C to save recordings safely

## Requirements

### Build Dependencies
- C++20 compatible compiler (GCC 10+, Clang 11+, MSVC 2019+)
- CMake 3.15+
- FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- Boost.Asio
- spdlog

### Platform-Specific
- **Windows**: DirectX 11
- **Linux**: X11 with MIT-SHM extension (optional, for better performance)

## Building

### Windows Setup (MSVC with Ninja)

When building on Windows with MSVC and Ninja, you need to set up the compiler environment. You have two options:

**Option 1: Open VS Code from the x64 Native Tools Command Prompt (Recommended)**

1. Open "x64 Native Tools Command Prompt for VS 2022"
2. Navigate to your project directory
3. Run: `code .`
4. Build normally in VS Code

This automatically sets up all required environment variables.

**Option 2: Configure CMake Presets with Environment Variables**

If you prefer to open VS Code normally, add these environment variables to your `CMakePresets.json`:

```json
"environment": {
    "PATH": "E:/Visual Studio 2022/Community/VC/Tools/MSVC/14.41.34120/bin/Hostx64/x64;C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64;$penv{PATH}",
    "INCLUDE": "E:/Visual Studio 2022/Community/VC/Tools/MSVC/14.41.34120/include;C:/Program Files (x86)/Windows Kits/10/include/10.0.22621.0/ucrt;C:/Program Files (x86)/Windows Kits/10/include/10.0.22621.0/um;C:/Program Files (x86)/Windows Kits/10/include/10.0.22621.0/shared;C:/Program Files (x86)/Windows Kits/10/include/10.0.22621.0/winrt",
    "LIB": "E:/Visual Studio 2022/Community/VC/Tools/MSVC/14.41.34120/lib/x64;C:/Program Files (x86)/Windows Kits/10/lib/10.0.22621.0/ucrt/x64;C:/Program Files (x86)/Windows Kits/10/lib/10.0.22621.0/um/x64"
}
```

Adjust the paths to match your Visual Studio and Windows SDK installation directories.

#### dependencies

1. download prebuilt ffmpeg from https://dufs.ko/installs/c%2B%2B/libs/ffmpeg-prebuilt 
2. extract to known location 
3. add to CMakePresets.txt cacheVariables using FFMPEG_ROOT=path/to/ffmpeg-prebuilt/

### Build Steps

```bash
# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build . --config Release
```

### REST API

The application exposes a REST API on port 8080:

#### Get Statistics
```bash
GET http://localhost:8080/stats
```

Response:
```json
{
    "framesCaptured": 1500,
    "framesEncoded": 1500,
    "framesDropped": 0,
    "averageFps": 30.0,
    "isRecording": true
}
```

#### Stop Recording and Save
```bash
POST http://localhost:8080/stop
Content-Type: application/json

{
    "outputPath": "recording.mp4"
}
```

Response:
```json
{
    "success": true,
    "message": "Recording stopped and saved"
}
```

### Command Line

Run the application:
```bash
./ScreenRecorderCPP
```

- Press **Ctrl+C** to stop recording and save the buffer
- The server listens on `http://localhost:8080`
- Stats are printed every 2 seconds

## Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `outputPath` | string | - | Path to save the video file |
| `fps` | int | 30 | Frames per second |
| `bitrate` | int | 4000000 | Video bitrate in bits/sec |
| `bufferDuration` | double | 10.0 | Circular buffer size in seconds |
| `monitorIndex` | int | 0 | Which monitor to capture (0 = primary) |
| `preferHardwareEncoding` | bool | true | Use hardware encoding if available |

### Key Components

- **IScreenRecorder**: Abstract interface for platform-independent API
- **ScreenRecorderBase**: Shared implementation for common functionality
- **Platform Implementations**: Windows (DirectX Desktop Duplication) and Linux (X11)
- **VideoEncoder**: FFmpeg-based video encoding
- **CircularFrameBuffer**: Thread-safe circular buffer for frame storage
- **ApiServer**: Boost.Beast-based REST API server
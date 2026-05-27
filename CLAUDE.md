# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**AVCapture** is a cross-platform screen recording application with:
- Hardware-accelerated H.264 video encoding via FFmpeg
- Audio capture and mixing (system audio + microphone, encoded to AAC)
- Circular/rolling segment buffer recorded as MPEG-TS files
- REST API server (Boost.Beast) for control and monitoring
- Master-worker distributed recording with nanosecond clock synchronization
- NFS remote file storage backend

## Build Commands

### Linux (Clang + Ninja)

```bash
# First-time: build FFmpeg and dependencies
./build.sh

# Configure
cmake --preset debug       # or: release, relwithdebinfo
# OR manually:
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -G Ninja ..

# Build
cmake --build build        # or: cmake --build . from inside build/

# Format code (run clang-format)
cmake --build build --target format
```

### Windows (MSVC + Ninja)

Open "x64 Native Tools Command Prompt for VS 2022", then `code .` to launch VS Code with the MSVC environment set up. Use `vs-debug`, `vs-release`, or `vs-relwithdebinfo` presets. Set `FFMPEG_ROOT` in `CMakePresets.json` to your FFmpeg installation path.

### Dependency Build Script (`build.sh`)

```bash
./build.sh [options]
  --only-x264 | --only-ffmpeg | --only-x11 | --only-zlib
  --skip-x264 | --skip-ffmpeg | --skip-x11 | --skip-zlib
  --keep-build     # don't delete build dirs after compilation
  --keep-downloads # keep downloaded archives
```

### FFmpeg Location

On Linux, FFmpeg is expected at `/usr/local` (override with `-DFFMPEG_ROOT=<path>`). On Windows, set `FFMPEG_ROOT` in `CMakePresets.json`.

## Architecture

### Entry Point & Lifecycle (`src/main.cpp`)

1. Load `settings.toml` → configure logging, platform recording config
2. Initialize `MediaRecorder` → creates platform-specific screen recorder, audio, file backend
3. Wire role-specific behavior:
   - **none** (standalone): immediately start recording, serve REST API
   - **kVideo** (master): start `SyncMasterServer`, auto-start timer, REST API
   - **kAudio** (worker): connect `SyncWorkerClient` to master, await sync commands
4. Run `asio::io_context` event loop (single-threaded async for API + sync networking)
5. On SIGINT/SIGTERM: call `MediaRecorder::save_and_upload()` then exit

### Core Components

| Component | Files | Responsibility |
|-----------|-------|----------------|
| `MediaRecorder` | `src/core/MediaRecorder.*` | Orchestrates all subsystems |
| `IScreenRecorder` / `ScreenRecorderBase` | `src/core/IScreenRecorder.hpp`, `ScreenRecorderBase.*` | Platform-agnostic recording loop with template method pattern |
| `ScreenRecorderLinux` | `src/platform/ScreenRecorderLinux.*` | X11 + MIT-SHM capture |
| `ScreenRecorderWindows` | `src/platform/ScreenRecorderWindows.*` | DirectX 11 Desktop Duplication |
| `VideoEncoder` | `src/core/VideoEncoder.*` | FFmpeg H.264 encoder (HW accel: nvenc, qsv, then SW fallback) |
| `VideoEncoderQueue` | `src/core/VideoEncoderQueue.*` | Lock-free queue of encoded frames |
| `AudioCapturer` + `AudioMixer` | `src/core/AudioCapturer.*`, `AudioMixer.*` | RtAudio capture → PCM mix → AAC encode |
| `RollingSegments` | `src/core/RollingSegments.*` | Sliding window of MPEG-TS segment files on disk |
| `ApiServer` / `Router` / `HttpSession` | `src/api/` | Boost.Beast HTTP/1.1 REST server |
| `SyncMasterServer` / `SyncWorkerClient` | `src/core/sync/` | Distributed recording with NTP-like clock sync |
| `NfsClient` / `IFileBackend` | `src/core/NfsClient.*`, `IFileBackend.hpp` | Local or NFS3 remote storage |

### Data Flow

```
ScreenRecorder (platform capture thread)
    └─► VideoEncoder → VideoEncoderQueue
                              │
AudioCapturer (RtAudio) → AudioMixer → encoded AAC frames
                              │
                     RollingSegments (segment thread)
                         └─► MPEG-TS files on disk (local or NFS)
                              │
                     ApiServer (async, on io_context)
                         └─► /stats, /stop, /start
```

### Error Handling Pattern

Uses `std::expected<T, Error>` throughout. The `Error` type (in `src/utils/error.hpp`) carries a source-location context stack. Always propagate with `with_context()` at call sites rather than logging and swallowing errors.

```cpp
Result<Foo> some_fn() {
    auto r = inner();
    if (!r) return std::unexpected(r.error().with_context("some_fn"));
    ...
}
```

### Logging

spdlog with named loggers (see `src/utils/log.hpp`). Loggers: `api`, `server`, `video.capture`, `video.encode`, `audio.capture`, `audio.encode`, `app`, `media_recorder`, `sync`. Logs go to `{exe_dir}/logs/` with rotation. Level is set in `settings.toml`.

## Configuration

Copy `settings-example.toml` to `settings.toml`. Key sections:

```toml
[recording]
fps = 30
bitrate = 4000000
buffer_duration = 10   # circular buffer seconds
segment_seconds = 2

[sync]
role = "none"          # none | video (master) | audio (worker)
peer_address = "..."
port = 45000

[nfs]
save_locally = false
server_address = "..."
export_path = "..."
```

## Key Conventions

- **C++26** standard; precompiled header at `src/pch.h` — add heavy/stable includes there
- Prefer `std::expected` / `Result<T>` over exceptions for recoverable errors
- Platform-specific code lives in `src/platform/`, guarded by `#ifdef _WIN32` / `#ifdef __linux__`
- Lock-free concurrency: `CircularBuffer<T>` and `moodycamel::ReaderWriterQueue` for cross-thread frame passing
- All async networking (API + sync) runs on the single shared `asio::io_context`; avoid blocking calls on it
- `.clang-format` defines code style — run the `format` CMake target before committing

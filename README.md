# AVCapture

AVCapture is a small C++ audio/video capture app. It records the screen together with microphone input and system audio into a rolling buffer, then lets you save the most recent buffered recording with `Ctrl+C` or through a local HTTP API.

It currently supports:

- Linux/X11 screen capture
- Windows/Desktop Duplication screen capture
- H.264 video through FFmpeg
- Microphone and system audio capture through RtAudio/PulseAudio on Linux
- Rolling segment buffer
- Local API endpoints for health, status, and save
- Per-user deploy scripts for Linux `systemd` and Windows Task Scheduler

## Repository Layout

```text
src/              application, recorder core, API server, platform capture
deploy/linux/     Linux deploy and undeploy scripts
deploy/win/       Windows deploy and undeploy scripts
third_party/      vendored/fetched dependency configuration and sources
build.sh          optional dependency build script for Linux
settings-example.toml
```

## Linux Dependencies

You can either install dependencies from your distro or use `build.sh` to build FFmpeg/x264/zlib/X11 support from source.

### Option 1: Use Apt Packages

On Ubuntu/Debian-like systems:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build git curl tar pkg-config \
  autoconf automake libtool nasm yasm python3 \
  ffmpeg libavcodec-dev libavformat-dev libavutil-dev \
  libswscale-dev libswresample-dev libavdevice-dev \
  libx264-dev zlib1g-dev \
  libx11-dev libxext-dev libxfixes-dev libxrandr-dev libxrender-dev \
  libxau-dev libxdmcp-dev libxcb1-dev libxcb-shm0-dev xorg-dev \
  libpulse-dev
```

The project uses C++26 in CMake, so use a compiler new enough for your toolchain. If your distro compiler is too old, install a newer GCC or Clang and pass it to CMake with `CMAKE_CXX_COMPILER`.

### Option 2: Use `build.sh`

`build.sh` builds and installs native Linux dependencies into `/usr/local`:

- x264
- zlib
- FFmpeg with the encoders/features this project uses
- X11 libraries fetched from upstream X.Org/XCB release servers

Run:

```bash
sudo ./build.sh
```

Useful options:

```bash
sudo ./build.sh --only-x11
sudo ./build.sh --only-ffmpeg
sudo ./build.sh --skip-x11
sudo ./build.sh --keep-build --keep-downloads
```

The X11 tarballs are downloaded from:

```text
https://www.x.org/releases/individual
https://xcb.freedesktop.org/dist
```

You can override those mirrors:

```bash
sudo XORG_RELEASES_URL=https://www.x.org/releases/individual \
     XCB_RELEASES_URL=https://xcb.freedesktop.org/dist \
     ./build.sh --only-x11
```

After using `build.sh`, configure CMake with FFmpeg rooted at `/usr/local`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFFMPEG_ROOT=/usr/local
cmake --build build -j
```

## Build The Project

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

If FFmpeg is installed somewhere custom:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFFMPEG_ROOT=/path/to/ffmpeg
cmake --build build -j
```

The executable is:

```text
build/AVCapture
```

## Configure

AVCapture loads `settings.toml` from the current working directory. Start from the example:

```bash
cp settings-example.toml build/settings.toml
```

Example:

```toml
[api]
address = "127.0.0.1"
port = 8084

[recording]
fps = 30
bitrate = 4000000
recording_length_seconds = 10
segment_buffer_seconds = 2

[audio]
output_device_name = ""
input_device_name  = ""

[log]
level = "info"
max_log_size_bytes = 5242880
max_files = 5
```

Important settings:

- `recording.recording_length_seconds`: how much history is kept in the rolling buffer.
- `recording.segment_buffer_seconds`: segment size used internally for rolling saves.
- `recording.fps`: capture frame rate.
- `recording.bitrate`: target H.264 bitrate.
- `api.address` / `api.port`: local HTTP API bind address.
- `audio.output_device_name`: system/output audio device. Leave empty to use the default output capture device.
- `audio.input_device_name`: microphone/input audio device. Leave empty to use the default input device.

## Run

Run from the directory that contains `settings.toml`:

```bash
cd build
./AVCapture
```

On `Ctrl+C` or `SIGTERM`, AVCapture saves the current rolling buffer to:

```text
recording.mp4
```

## HTTP API

Assuming the default example port `8084`:

```bash
curl http://127.0.0.1:8084/health
```

```json
{"status":"healthy"}
```

```bash
curl http://127.0.0.1:8084/status
```

```json
{"recording":true}
```

Save the current buffer:

```bash
curl -X POST http://127.0.0.1:8084/stop
```

The saved file is currently named `recording.mp4` in the process working directory.

## Linux Deploy

The Linux deploy scripts install AVCapture as a per-user `systemd` service. They should usually be run as the desktop user, not root. If you accidentally run them with `sudo`, they try to re-run as the original login user.

Prepare a deploy folder:

```bash
mkdir -p deploy/linux/runtime
cp build/AVCapture deploy/linux/runtime/
cp settings-example.toml deploy/linux/runtime/settings.toml
cp deploy/linux/deploy.sh deploy/linux/undeploy.sh deploy/linux/runtime/
```

Edit `deploy/linux/runtime/settings.toml`, then install and start:

```bash
cd deploy/linux/runtime
./deploy.sh
```

Check status:

```bash
systemctl --user status AVCapture.service
journalctl --user -u AVCapture.service -f
```

Remove:

```bash
cd deploy/linux/runtime
./undeploy.sh
```

The service uses the deploy folder as `WorkingDirectory`, so keep `settings.toml` next to the executable.

## Windows Build And Deploy

Build with Visual Studio/MSVC or another CMake-compatible Windows toolchain. If using Ninja with MSVC, open the project from a Visual Studio Developer Command Prompt so the compiler environment is initialized.

For deployment, place these files in one folder:

```text
AVCapture.exe
settings.toml
deploy.bat
undeploy.bat
deploy_task.ps1
```

Then run `deploy.bat`. It installs a Windows scheduled task named `AVCapture`, starts it at user logon, and runs it immediately.

To remove it, run:

```bat
undeploy.bat
```

## Formatting

Format source files with:

```bash
./format.sh
```

Or use the CMake format target if configured:

```bash
cmake --build build --target format
```

## Notes

- Linux screen capture currently targets X11. Wayland sessions may not expose the same screen capture path.
- Hardware H.264 encoders are attempted before CPU x264 when available in the FFmpeg build.
- The project binds the API to `127.0.0.1` by default. Keep it local unless you intentionally want remote control.

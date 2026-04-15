#ifdef __linux__

    #include "spdlog/spdlog.h"
    #include "ScreenRecorderLinux.hpp"
    #include <X11/Xutil.h>
    #include <X11/extensions/XShm.h>
    #include <X11/extensions/Xfixes.h>
    #include <X11/extensions/Xrandr.h>
    #include <libavutil/pixfmt.h>
    #include <sys/ipc.h>
    #include <sys/shm.h>
    #include "../utils/log.hpp"

using namespace VSCapture::Platform;
using namespace VSCapture::Error;
using namespace VSCapture::Log;
using namespace VSCapture;

ScreenRecorderLinux::ScreenRecorderLinux() = default;

ScreenRecorderLinux::~ScreenRecorderLinux() {
    stop();
    cleanup_platform();
}

VoidResult ScreenRecorderLinux::init_platform() {
    if (XInitThreads() == 0) {
        Log::video_capture()->warn("XInitThreads() failed; Xlib calls may not be thread-safe");
    }

    // Open display
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) {
        return std::unexpected(make_error().with_context("Failed to open X display"));
    }

    // Get the default screen index and its root window
    screen_num_ = DefaultScreen(display_);
    root_window_ = RootWindow(display_, screen_num_);

    // Get screen dimensions
    XWindowAttributes window_attrs;
    XGetWindowAttributes(display_, root_window_, &window_attrs);
    width_ = window_attrs.width;
    height_ = window_attrs.height;

    // Detect rotation using XRandR
    auto rotation_result = detect_screen_rotation();
    if (!rotation_result) {
        // If XRandR detection fails, fall back to no rotation
        Log::video_capture()->warn(
            "Failed to detect screen rotation: {}. Defaulting to no rotation.",
            rotation_result.error().what());
        rotation_ = Core::RotationType::kRotationIdentity;
    }
    else {
        rotation_ = rotation_result.value();
    }

    Log::video_capture()->info("Rotation: {}", static_cast<int>(rotation_));
    Log::video_capture()->info("Screen resolution: {}x{}", width_, height_);

    // Allocate rotation buffer if needed (done in base class prepare_frame_data)
    if (rotation_ != Core::RotationType::kRotationIdentity && rotation_ != Core::RotationType::kUnspecified) {
        size_t buffer_size = width_ * height_ * 4; // BGRA format
        rotation_buffer_.resize(buffer_size);
        Log::video_capture()->info("Allocated rotation buffer: {} bytes", buffer_size);
    }

    // Try to use MIT-SHM extension, boosts performance by sharing memory between
    // X and us
    int major = 0;
    int minor = 0;
    Bool pixmaps = 0;

    if (XShmQueryVersion(display_, &major, &minor, &pixmaps) != 0) {
        use_shm_ = true;
        Log::video_capture()->info("Using MIT-SHM extension for screen capture");

        // Create an XImage whose pixel buffer will live in the shared memory
        ximage_ = XShmCreateImage(
            display_,
            DefaultVisual(display_, screen_num_),
            DefaultDepth(display_, screen_num_),
            ZPixmap,
            nullptr,
            &shminfo_,
            width_,
            height_);

        if (ximage_ == nullptr) {
            // If creating a SHM-backend image failed, fallback to standard path.
            Log::video_capture()->warn("Failed to create SHM image, falling back to standard XGetImage");
            use_shm_ = false;
        }
        else {
            // Allocate shared memory segment for the image's pixel data
            // Size = bytes_per_line * height (stride * rows)
            shminfo_.shmid = shmget(IPC_PRIVATE, ximage_->bytes_per_line * ximage_->height, IPC_CREAT | 0777);

            if (shminfo_.shmid < 0) {
                Log::video_capture()->warn(
                    "Failed to allocate shared memory, falling back to "
                    "standard XGetImage");
                XDestroyImage(ximage_);
                ximage_ = nullptr;
                use_shm_ = false;
            }
            else {
                // Map the shared memory into our address space and assign it to the
                // XImage
                shminfo_.shmaddr = ximage_->data = static_cast<char*>(shmat(shminfo_.shmid, nullptr, 0));
                shminfo_.readOnly = False; // We will write to it (X server writes, we read)

                // Attach the shared memory segment into the X server side
                if (XShmAttach(display_, &shminfo_) == 0) {
                    // If attach fails, unmap and remove the segment
                    Log::video_capture()->warn(
                        "Failed to attach shared memory, falling back to "
                        "standard XGetImage");
                    shmdt(shminfo_.shmaddr);
                    shmctl(shminfo_.shmid, IPC_RMID, nullptr);
                    XDestroyImage(ximage_);
                    ximage_ = nullptr;
                    use_shm_ = false;
                }
                else {
                    // Mark segment for deletion, it will be removed when all detach
                    shmctl(shminfo_.shmid, IPC_RMID, nullptr);
                }
            }
        }
    }
    else {
        // SHM is not available on this server, we will use XGetImage on each frame
        Log::video_capture()->info("MIT-SHM extension not available, using standard XGetImage");
        use_shm_ = false;
    }

    return {};
}

Result<Core::RotationType> ScreenRecorderLinux::detect_screen_rotation() {
    // Check if XRandR extension is available
    int event_base = 0;
    int error_base = 0;
    if (XRRQueryExtension(display_, &event_base, &error_base) == 0) {
        return std::unexpected(make_error().with_context("XRandR extension not available"));
    }

    // Get XRandR version
    int major_version = 0;
    int minor_version = 0;
    if (XRRQueryVersion(display_, &major_version, &minor_version) == 0) {
        return std::unexpected(make_error().with_context("Failed to query XRandR version"));
    }

    Log::video_capture()->debug("XRandR version: {}.{}", major_version, minor_version);

    // Get screen resources
    XRRScreenResources* screen_resources = XRRGetScreenResourcesCurrent(display_, root_window_);
    if (screen_resources == nullptr) {
        return std::unexpected(make_error().with_context("Failed to get XRandR screen resources"));
    }

    // Get primary output for rotation
    RROutput primary = XRRGetOutputPrimary(display_, root_window_);

    if (primary == None) {
        XRRFreeScreenResources(screen_resources);
        return std::unexpected(make_error().with_context("No outputs available"));
    }

    // Get output info
    XRROutputInfo* output_info = XRRGetOutputInfo(display_, screen_resources, primary);
    if (output_info == nullptr) {
        XRRFreeScreenResources(screen_resources);
        return std::unexpected(make_error().with_context("Failed to get output info"));
    }

    // Check if output is connected and has a CRTC
    if (output_info->connection != RR_Connected || output_info->crtc == None) {
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_resources);
        return std::unexpected(make_error().with_context("Output is not connected or has no CRTC"));
    }

    // Get CRTC info to get rotation
    XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(display_, screen_resources, output_info->crtc);
    if (crtc_info == nullptr) {
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_resources);
        return std::unexpected(make_error().with_context("Failed to get CRTC info"));
    }

    // Get the rotation value
    Rotation rotation = crtc_info->rotation;

    Log::video_capture()->debug("XRandR rotation value: 0x{:x}", rotation);

    // Clean up XRandR resources
    XRRFreeCrtcInfo(crtc_info);
    XRRFreeOutputInfo(output_info);
    XRRFreeScreenResources(screen_resources);

    // Convert XRandR rotation to our rotation type
    // XRandR uses bit flags: RR_Rotate_0, RR_Rotate_90, RR_Rotate_180, RR_Rotate_270
    Core::RotationType result;

    if ((rotation & RR_Rotate_0) != 0) {
        result = Core::RotationType::kRotationIdentity;
    }
    else if ((rotation & RR_Rotate_90) != 0) {
        result = Core::RotationType::kRotationRotate90;
    }
    else if ((rotation & RR_Rotate_180) != 0) {
        result = Core::RotationType::kRotationRotate180;
    }
    else if ((rotation & RR_Rotate_270) != 0) {
        result = Core::RotationType::kRotationRotate270;
    }
    else {
        result = Core::RotationType::kUnspecified;
    }

    return result;
}

void ScreenRecorderLinux::capture_frame() {
    // Final image pointer: either the persistent SHM XImage or a one off
    // XGetImage
    XImage* image = nullptr;

    if (use_shm_) {
        // Flush/Sync pending X requests to ensure capture reflects current screen
        XSync(display_, False);

        // Populate the SHM-backend XImage with current screen pixels (fast path by
        // using SHM)
        if (XShmGetImage(display_, root_window_, ximage_, 0, 0, AllPlanes) == 0) {
            Log::video_capture()->error("Failed to capture screen with SHM");

            // If we have a previous frame, re-encode it to maintain frame rate
            if (last_frame_ != nullptr) {
                Log::video_capture()->trace("Reusing last frame due to capture failure");

                int64_t pts = get_next_frame_pts();

                if (encoder_queue_.push_frame(last_frame_, final_stride_, pts)) {
                    Log::video_capture()->trace("Pushed to encoder queue");
                }
                else {
                    Log::video_capture()->warn("Failed pushing frame to encoder queue");
                }
            }
            return;
        }
        image = ximage_;
    }
    else {
        // Fallback:
        // Flush/Sync pending X requests to ensure capture reflects current screen
        XSync(display_, False);

        // Allocate a fresh XImage for the current frame
        image = XGetImage(display_, root_window_, 0, 0, width_, height_, AllPlanes, ZPixmap);
        if (image == nullptr) {
            Log::video_capture()->error("Failed to capture screen with XGetImage");

            // If we have a previous frame, re-encode it to maintain frame rate
            if (last_frame_ != nullptr) {
                Log::video_capture()->trace("Reusing last frame due to capture failure");

                int64_t pts = get_next_frame_pts();

                if (encoder_queue_.push_frame(last_frame_, final_stride_, pts)) {
                    Log::video_capture()->trace("Pushed to encoder queue");
                }
                else {
                    Log::video_capture()->warn("Failed pushing frame to encoder queue");
                }
            }
            return;
        }
    }

    // Validate format: we expect 32bpp (BGRA/RGBA)
    if (image->bits_per_pixel != 32) {
        Log::video_capture()->error("Expected 32bpp, got {}", image->bits_per_pixel);
        if (!use_shm_ && (image != nullptr)) {
            XDestroyImage(image);
        }

        // If we have a previous frame, re-encode it to maintain frame rate
        if (last_frame_ != nullptr) {
            Log::video_capture()->trace("Reusing last frame due to format mismatch");

            int64_t pts = get_next_frame_pts();

            if (encoder_queue_.push_frame(last_frame_, final_stride_, pts)) {
                Log::video_capture()->trace("Pushed to encoder queue");
            }
            else {
                Log::video_capture()->warn("Failed pushing frame to encoder queue");
            }
        }
        return;
    }

    // PTS (presentation timestamp) generation
    int64_t pts = get_next_frame_pts();

    // Prepare frame data (rotates if needed) - uses base class method
    last_frame_ = prepare_frame_data(reinterpret_cast<uint8_t*>(image->data), image->bytes_per_line, final_stride_);

    // Feed the raw frame to the video encoder
    if (encoder_queue_.push_frame(last_frame_, final_stride_, pts)) {
        Log::video_capture()->trace("Pushed to encoder queue");
    }
    else {
        Log::video_capture()->warn("Failed pushing frame to encoder queue");
    }

    // Clean up non-SHM image
    if (!use_shm_ && (image != nullptr)) {
        XDestroyImage(image);
    }
}

void ScreenRecorderLinux::cleanup_platform() {
    // Release X11 resources
    if (use_shm_ && (ximage_ != nullptr)) {
        XShmDetach(display_, &shminfo_);
        XDestroyImage(ximage_);
        // Drops reference count for X's / our's shared memory segment
        shmdt(shminfo_.shmaddr);
        ximage_ = nullptr;
    }

    if (display_ != nullptr) {
        XCloseDisplay(display_);
        display_ = nullptr;
    }

    is_initialized_.store(false, std::memory_order::relaxed);
}

Core::RotationType ScreenRecorderLinux::get_rotation_type(int rotation) {
    switch (rotation) {
    case 0: return Core::RotationType::kUnspecified;
    case 1: return Core::RotationType::kRotationIdentity;
    case 2: return Core::RotationType::kRotationRotate90;
    case 3: return Core::RotationType::kRotationRotate180;
    case 4: return Core::RotationType::kRotationRotate270;
    }

    return Core::RotationType::kUnspecified;
}

#endif // __linux__
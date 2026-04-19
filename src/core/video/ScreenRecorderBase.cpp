
#include "VideoConfig.hpp"
#include "ScreenRecorderBase.hpp"

using namespace VSCapture::Platform;
using namespace VSCapture::Error;
using namespace VSCapture;
VoidResult ScreenRecorderBase::initialize(const Core::VideoConfig& config) {
    if (is_initialized_.load(std::memory_order::relaxed)) {
        return std::unexpected(make_error().with_context("Failed to initialize since object is already initialized"));
    }

    config_ = config;

    if (auto res = init_platform(); !res) {
        return std::unexpected(res.error().with_context("Failed initializing platform specific screen recorder"));
    }

    // Initialize encoder
    Core::VideoConfig encoder_config{};

    // flip screen and reinitialize encoder if rotated 90 or 270 degrees
    if (rotation_ == Core::RotationType::kRotationRotate90 || rotation_ == Core::RotationType::kRotationRotate270) {
        encoder_config.width_ = height_;
        encoder_config.height_ = width_;
    }
    else {
        encoder_config.width_ = width_;
        encoder_config.height_ = height_;
    }

    encoder_config.fps_ = config_.fps_;
    encoder_config.bitrate_ = config_.bitrate_;
    encoder_config.rotation_ = rotation_;
    encoder_config.recording_length_seconds_ = config.recording_length_seconds_;
    encoder_config.monitor_index_ = config.monitor_index_;

    if (auto res = encoder_queue_.initialize(encoder_config); !res) {
        cleanup_platform();
        return std::unexpected(res.error().with_context("Failed initializing video encoder thread"));
    }

    is_initialized_.store(true, std::memory_order::relaxed);

    return {};
}

VoidResult ScreenRecorderBase::start() {
    if (!is_initialized_.load(std::memory_order::relaxed)) {
        return std::unexpected(make_error().with_context("Failed to start since object is not initialized"));
    }

    if (is_recording_.load(std::memory_order::relaxed)) {
        return std::unexpected(make_error().with_context("Failed to start since recording thread is already running"));
    }

    is_recording_.store(true, std::memory_order::relaxed);
    recording_start_time_ = std::chrono::steady_clock::now();

    encoder_queue_.start();

    record_thread_ = std::jthread([this](std::stop_token st) { recording_loop(st); });

    return {};
}

void ScreenRecorderBase::stop() {
    if (!is_recording_.load(std::memory_order::relaxed)) {
        return;
    }

    is_recording_.store(false, std::memory_order::relaxed);

    record_thread_.request_stop();
    record_thread_.join();

    encoder_queue_.stop();
}

bool ScreenRecorderBase::is_recording() const {
    return is_recording_.load(std::memory_order::relaxed);
}

void ScreenRecorderBase::recording_loop(std::stop_token st) {
    auto next_frame_time = std::chrono::steady_clock::now();
    const auto frame_duration = std::chrono::microseconds(1'000'000 / config_.fps_);

    while (!st.stop_requested()) {
        // Capture frame
        capture_frame();

        // Calculate next frame time
        next_frame_time += frame_duration;

        // Sleep until next frame is due
        auto now = std::chrono::steady_clock::now();
        if (next_frame_time > now) {
            std::this_thread::sleep_until(next_frame_time);
        }
        else {
            // We're running behind schedule, don't sleep
            next_frame_time = now;
        }
    }
}

uint8_t* ScreenRecorderBase::prepare_frame_data(uint8_t* src_data, int src_stride, int& out_stride) {
    // No rotation needed
    if (rotation_ == Core::RotationType::kUnspecified || rotation_ == Core::RotationType::kRotationIdentity) {
        out_stride = src_stride;
        return src_data;
    }

    // Allocate rotation buffer only once (on first use or size change)
    size_t needed_size = width_ * height_ * 4;
    if (rotation_buffer_.size() != needed_size) {
        rotation_buffer_.resize(needed_size);
    }

    uint8_t* dst = rotation_buffer_.data();

    // TODO is there a better way of doing it instead of repeating the for's
    // Rotate based on rotation value
    if (rotation_ == Core::RotationType::kRotationRotate90) { // 90 degrees clockwise
        out_stride = height_ * 4;

        // Input: width x height -> Output: height x width
        // new[y][x] = old[y][height - 1 - x] rotated
        for (int y = 0; y < height_; ++y) {
            const uint8_t* src_row = src_data + y * src_stride;
            for (int x = 0; x < width_; ++x) {
                uint32_t* dst_pixel = (uint32_t*)(dst + x * height_ * 4 + (height_ - 1 - y) * 4);
                uint32_t* src_pixel = (uint32_t*)(src_row + x * 4);
                *dst_pixel = *src_pixel;
            }
        }
    }
    else if (rotation_ == Core::RotationType::kRotationRotate180) { // 180 degrees clockwise
        out_stride = width_ * 4;

        // Input: width x height -> Output: (same dimensions)
        // new[y][x] = old[height - 1 - y][width - 1 - x]
        for (int y = 0; y < height_; ++y) {
            const uint8_t* src_row = src_data + y * src_stride;
            uint8_t* dst_row = dst + (height_ - 1 - y) * width_ * 4;

            for (int x = 0; x < width_; ++x) {
                uint32_t* dst_pixel = (uint32_t*)(dst_row + (width_ - 1 - x) * 4);
                uint32_t* src_pixel = (uint32_t*)(src_row + x * 4);
                *dst_pixel = *src_pixel;
            }
        }
    }
    else if (rotation_ == Core::RotationType::kRotationRotate270) { // 270 degrees clockwise (90 counter-clockwise)
        out_stride = height_ * 4;

        // Input: width x height -> Output: height x width
        // new[y][x] = old[x][width - 1 - y] rotated
        for (int y = 0; y < height_; ++y) {
            const uint8_t* src_row = src_data + y * src_stride;
            for (int x = 0; x < width_; ++x) {
                // Output position: row = width - 1 - x, col = y
                uint32_t* dst_pixel = (uint32_t*)(dst + (width_ - 1 - x) * height_ * 4 + y * 4);
                uint32_t* src_pixel = (uint32_t*)(src_row + x * 4);
                *dst_pixel = *src_pixel;
            }
        }
    }

    return dst;
}

int64_t ScreenRecorderBase::get_next_frame_pts() {
    // Video pts should be a monotonic counter of each frame
    return last_frame_pts_++;
}

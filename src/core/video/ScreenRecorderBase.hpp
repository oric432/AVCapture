#pragma once

#include "IScreenRecorder.hpp"
#include "types.hpp"
#include "VideoConfig.hpp"
#include "VideoEncoderQueue.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace VSCapture::Platform {

/**
 * @brief Base class with common implementation for platform-specific recorders
 */
class ScreenRecorderBase : public IScreenRecorder {
public:
    ~ScreenRecorderBase() override = default;

    // Common IScreenRecorder implementations
    Error::VoidResult initialize(const Core::VideoConfig& config) override;
    Error::VoidResult start() override;
    void stop() override;
    bool is_recording() const override;
    std::vector<EncodedVideoFrame> get_frames() const override { return encoder_queue_.get_encoded_frames(); }
    void clear_frames() override { encoder_queue_.clear_frames(); }
    AVCodecContext* get_encoder_context() const override { return encoder_queue_.get_encoder_context(); }

    uint8_t* prepare_frame_data(uint8_t* src_data, int src_stride, int& out_stride) override;

protected:
    // Platform-specific methods that must be implemented
    virtual Error::VoidResult init_platform() = 0;
    virtual void capture_frame() = 0;
    virtual void cleanup_platform() = 0;
    virtual Core::RotationType get_rotation_type(int rotation) = 0;

    // Common recording loop
    void recording_loop(std::stop_token st);
    int64_t get_next_frame_pts();

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    // Protected members are intentional here - derived classes need direct access
    // to these members for implementing platform-specific capture logic

    Core::VideoConfig config_;
    Encoding::VideoEncoderQueue encoder_queue_;

    // For potential screen rotation
    std::vector<uint8_t> rotation_buffer_;
    Core::RotationType rotation_;

    int32_t width_ = 0;
    int32_t height_ = 0;

    std::atomic<bool> is_recording_{false};
    std::atomic<bool> is_initialized_{false};
    std::jthread record_thread_;

    std::chrono::steady_clock::time_point recording_start_time_;
    int64_t last_frame_pts_ = 0;

    uint8_t* last_frame_;
    int final_stride_;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

} // namespace VSCapture::Platform
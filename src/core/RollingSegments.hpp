#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <thread>
#include "utils/error.hpp"
#include "audio/AudioCapturer.hpp"
#include "video/IScreenRecorder.hpp"

extern "C" {
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

namespace VSCapture::Core {

class RollingSegment {
public:
    struct Config {
        std::filesystem::path dir_;
        int segment_seconds_ = 10;
        size_t ring_size_ = 180;
        VSCapture::RoleType role_type = VSCapture::RoleType::kNone;
    };

    RollingSegment() = default;
    ~RollingSegment();

    Error::VoidResult
    initialize(const Config& cfg, Platform::IScreenRecorder* screen_recorder, AudioCapturer* audio_capturer);

    Error::VoidResult start();
    void stop() noexcept;

    Error::VoidResult export_last_segments(size_t segments, const std::filesystem::path& out_mp4);

private:
    Error::VoidResult tick_save_one_segment();
    std::filesystem::path seg_path(size_t idx);

    Error::VoidResult save_av_to_ts(const std::filesystem::path& ts_path);
    Error::VoidResult save_a_to_ts(const std::filesystem::path& ts_path);
    Error::VoidResult save_v_to_ts(const std::filesystem::path& ts_path);

    Error::VoidResult write_packet_to_stream(
        AVFormatContext* fmt_ctx,
        AVStream* stream,
        AVRational src_time_base,
        const uint8_t* data,
        size_t size,
        int64_t pts,
        int64_t dts,
        bool is_key_frame);

    Config config_{};

    Platform::IScreenRecorder* screen_recorder_{nullptr};
    AudioCapturer* audio_capturer_{nullptr};

    std::atomic<bool> running_{false};
    std::thread th_;

    std::atomic<uint64_t> seq_{0};
};

} // namespace VSCapture::Core
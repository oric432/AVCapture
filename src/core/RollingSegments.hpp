#pragma once

#include "audio/AudioCapturer.hpp"
#include "utils/error.hpp"
#include "video/IScreenRecorder.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
}

namespace AVCapture::Core {

class RollingSegment {
public:
  struct Config {
    std::filesystem::path dir_;
    int segment_buffer_seconds_ = 10;
    size_t ring_size_ = 180;
  };

  RollingSegment() = default;
  ~RollingSegment();

  Error::VoidResult initialize(const Config &cfg,
                               Platform::IScreenRecorder *screen_recorder,
                               AudioCapturer *audio_capturer);

  Error::VoidResult start();
  void stop() noexcept;

  Error::VoidResult export_last_segments(size_t segments,
                                         const std::filesystem::path &out_mp4);

private:
  Error::VoidResult tick_save_one_segment();
  std::filesystem::path seg_path(size_t idx);

  Error::VoidResult create_segment_dir();
  void remove_segment_dir() noexcept;

  Error::VoidResult save_av_to_ts(const std::filesystem::path &ts_path);

  Error::VoidResult write_packet_to_stream(AVFormatContext *fmt_ctx,
                                           AVStream *stream,
                                           AVRational src_time_base,
                                           const uint8_t *data, size_t size,
                                           int64_t pts, int64_t dts,
                                           bool is_key_frame);

  Config config_{};

  Platform::IScreenRecorder *screen_recorder_{nullptr};
  AudioCapturer *audio_capturer_{nullptr};

  std::jthread th_;

  std::atomic<uint64_t> seq_{0};

  // Guards segment files on disk against concurrent access between the tick
  // thread (writing new segments) and export_last_segments (reading them,
  // possibly called from a background save thread).
  std::mutex segment_mutex_;
};

} // namespace AVCapture::Core
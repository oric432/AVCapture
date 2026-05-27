#include "MediaRecorder.hpp"
#include "RecordingConfig.hpp"
#include "RollingSegments.hpp"
#include "audio/AudioCapturer.hpp"
#include "audio/AudioConfig.hpp"
#include "video/VideoConfig.hpp"

using namespace VSCapture::Core;
using namespace VSCapture::Error;
using namespace VSCapture;

MediaRecorder::MediaRecorder() = default;

MediaRecorder::~MediaRecorder() { stop(); }

VoidResult MediaRecorder::initialize(RecordingConfig &recorder_config) {
  recorder_config_ = recorder_config;

  {
    auto screen_recorder = Platform::create_screen_recorder();
    if (!screen_recorder) {
      Log::crash_error("Could not create screen recording");
    }

    screen_recorder_ = std::move(screen_recorder);

    VideoConfig video_config;

    video_config.fps_ = recorder_config.video.fps_;
    video_config.bitrate_ = recorder_config.video.bitrate_;
    video_config.recording_length_seconds_ =
        recorder_config.video.segment_buffer_seconds_;

    if (auto res = screen_recorder_->initialize(video_config); !res) {
      return std::unexpected(
          res.error().with_context("Failed to initialize screen recorder"));
    }
  }

  {
    AudioConfig audio_config;

    audio_config.sample_rate_ = recorder_config.audio.sample_rate_;
    audio_config.channels_ = recorder_config.audio.channels_;
    audio_config.bitrate_ = recorder_config.audio.bitrate_;
    audio_config.recording_length_seconds_ =
        recorder_config.video.segment_buffer_seconds_;
    audio_config.buffer_frame_size_ = recorder_config.audio.buffer_frame_size_;
    audio_config.output_device_name_ =
        recorder_config.audio.output_device_name_;
    audio_config.input_device_name_ = recorder_config.audio.input_device_name_;

    if (auto res = audio_capturer_.initialize(audio_config); !res) {
      return std::unexpected(
          res.error().with_context("Failed to initialize audio capturer"));
    }
  }

  Core::RollingSegment::Config segment_config;
  segment_config.dir_ =
      std::filesystem::temp_directory_path() / "VSCapture" / "ring";
  segment_config.segment_buffer_seconds_ =
      static_cast<int>(recorder_config.video.segment_buffer_seconds_);
  segment_config.ring_size_ =
      static_cast<size_t>(recorder_config.video.recording_length_seconds_ /
                          recorder_config.video.segment_buffer_seconds_);

  if (auto res = segmenter_.initialize(segment_config, screen_recorder_.get(),
                                       &audio_capturer_);
      !res) {
    return std::unexpected(
        res.error().with_context("Failed to initialize rolling segmenter"));
  }

  return {};
}
VoidResult MediaRecorder::start() {
  if (auto res = segmenter_.start(); !res) {
    return std::unexpected(
        res.error().with_context("Failed starting rolling segmenter"));
  }

  if (auto res = screen_recorder_->start(); !res) {
    return std::unexpected(
        res.error().with_context("Failed starting screen recorder"));
  }

  if (auto res = audio_capturer_.start(); !res) {
    return std::unexpected(
        res.error().with_context("Failed starting audio capturer"));
  }

  return {};
}

void MediaRecorder::stop() {
  segmenter_.stop();
  audio_capturer_.stop();
  screen_recorder_->stop();
}

bool MediaRecorder::is_recording() {
  return audio_capturer_.is_capturing() || screen_recorder_->is_recording();
}

Result<std::string>
MediaRecorder::save_recording(std::string_view output_file) {
  size_t segments_to_export =
      static_cast<size_t>(recorder_config_.video.recording_length_seconds_ /
                          recorder_config_.video.segment_buffer_seconds_);

  if (auto res =
          segmenter_.export_last_segments(segments_to_export, output_file);
      !res) {
    return std::unexpected(
        res.error().with_context("Failed to export recording"));
  }

  return std::string{output_file};
}
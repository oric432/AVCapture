#pragma once

#include "RecordingConfig.hpp"
#include "RollingSegments.hpp"
#include "audio/AudioCapturer.hpp"
#include "utils/error.hpp"
#include "video/IScreenRecorder.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace AVCapture::Core {

class MediaRecorder {
public:
  MediaRecorder();
  ~MediaRecorder();

  Error::VoidResult initialize(Core::RecordingConfig &recorder_config);
  Error::VoidResult start();
  void stop();
  bool is_recording();

  // Saves the current buffer to a timestamped file inside the configured
  // output directory. Does not stop the recorder. Blocks until the export
  // (ffmpeg mux) completes.
  Error::Result<std::string> save_recording();

  // Same as save_recording(), but the export runs on a background thread so
  // callers on the API's io_context don't block while ffmpeg runs. Returns
  // the path the recording will be saved to; export failures are logged,
  // not returned.
  Error::Result<std::string> save_recording_async();

private:
  Error::Result<std::filesystem::path> make_output_path() const;
  Error::Result<std::string> save_recording_to(std::string_view output_file);

  std::unique_ptr<Platform::IScreenRecorder> screen_recorder_;
  Core::RollingSegment segmenter_;
  Core::AudioCapturer audio_capturer_;

  Core::RecordingConfig recorder_config_;

  // Background thread for save_recording_async(). Declared last so it is
  // destroyed (and joined) before the members it uses while running.
  std::jthread save_thread_;
};
} // namespace AVCapture::Core
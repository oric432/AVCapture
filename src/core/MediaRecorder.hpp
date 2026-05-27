#pragma once

#include "RecordingConfig.hpp"
#include "RollingSegments.hpp"
#include "audio/AudioCapturer.hpp"
#include "utils/error.hpp"
#include "video/IScreenRecorder.hpp"
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace VSCapture::Core {

class MediaRecorder {
public:
  MediaRecorder();
  ~MediaRecorder();

  Error::VoidResult initialize(Core::RecordingConfig &recorder_config);
  Error::VoidResult start();
  void stop();
  bool is_recording();

  Error::Result<std::string> save_recording(std::string_view output_file);

private:
  std::unique_ptr<Platform::IScreenRecorder> screen_recorder_;
  Core::RollingSegment segmenter_;
  Core::AudioCapturer audio_capturer_;

  Core::RecordingConfig recorder_config_;
};
} // namespace VSCapture::Core
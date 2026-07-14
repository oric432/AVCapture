#pragma once

#include <core/audio/AudioConfig.hpp>
#include <core/video/VideoConfig.hpp>
#include <string>

namespace AVCapture::Core {

/**
 * @brief Configuration for screen recording
 */
struct RecordingConfig {
  VideoConfig video;
  AudioConfig audio;
  std::string output_directory;
};

} // namespace AVCapture::Core

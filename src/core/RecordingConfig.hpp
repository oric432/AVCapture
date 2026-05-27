#pragma once

#include <core/audio/AudioConfig.hpp>
#include <core/video/VideoConfig.hpp>

namespace AVCapture::Core {

/**
 * @brief Configuration for screen recording
 */
struct RecordingConfig {
  VideoConfig video;
  AudioConfig audio;
};

} // namespace AVCapture::Core

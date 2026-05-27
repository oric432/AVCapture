#pragma once

#include <core/audio/AudioConfig.hpp>
#include <core/video/VideoConfig.hpp>

namespace VSCapture::Core {

/**
 * @brief Configuration for screen recording
 */
struct RecordingConfig {
  VideoConfig video;
  AudioConfig audio;
};

} // namespace VSCapture::Core

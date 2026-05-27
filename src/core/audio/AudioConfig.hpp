#pragma once

#include <cstdint>
#include <string>

namespace AVCapture::Core {

/**
 * @brief Audio configuration
 */
struct AudioConfig {
  int32_t sample_rate_ = 8000;
  int32_t channels_ = 1;
  int32_t bitrate_ = 48000;
  double recording_length_seconds_ = 10.0;
  unsigned int buffer_frame_size_ = 1024;
  std::string output_device_name_;
  std::string input_device_name_;
};
} // namespace AVCapture::Core
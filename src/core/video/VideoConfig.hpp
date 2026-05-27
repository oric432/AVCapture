#pragma once

namespace AVCapture::Core {

/**
 * @brief Configuration for video encoding
 */

enum class RotationType : std::uint8_t {
  kUnspecified,
  kRotationIdentity,
  kRotationRotate90,
  kRotationRotate180,
  kRotationRotate270
};

struct VideoConfig {
  int width_;
  int height_;
  int fps_ = 5;
  int bitrate_ = 4'000'000; // 4 Mbps
  double recording_length_seconds_ = 10.0;
  double segment_buffer_seconds_ = 2.0;
  RotationType rotation_ =
      RotationType::kUnspecified; // 0=normal, 1=90deg, 2=180deg, 3=270deg
};
} // namespace AVCapture::Core
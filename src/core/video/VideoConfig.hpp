#pragma once

namespace VSCapture::Core {

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
    int fps_;
    int bitrate_;
    int32_t monitor_index_ = 0;
    double buffer_duration_ = 10.0;
    RotationType rotation_ = RotationType::kUnspecified; // 0=normal, 1=90deg, 2=180deg, 3=270deg
};
} // namespace VSCapture::Core
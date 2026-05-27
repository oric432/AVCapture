#pragma once

#include "VideoConfig.hpp"
#include "types.hpp"
#include "utils/error.hpp"
#include <cstdint>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace AVCapture::Platform {
/**
 * @brief Abstract interface for platform-specific screen recording
 *
 * This interface provides a common API for screen recording across different
 * platforms. Platform-specific implementations handle the details of screen
 * capture and encoding.
 */
class IScreenRecorder {
public:
  virtual ~IScreenRecorder() = default;

  /**
   * @brief Initialize the screen recorder
   * @param config Recording configuration
   * @return true if initialization succeeded, false otherwise
   */
  virtual Error::VoidResult initialize(const Core::VideoConfig &config) = 0;

  /**
   * @brief Start recording
   * @return true if recording started successfully, false otherwise
   */
  virtual Error::VoidResult start() = 0;

  /**
   * @brief Stop recording and finalize the output file
   */
  virtual void stop() = 0;

  /**
   * @brief Check if currently recording
   * @return true if recording is in progress
   */
  [[nodiscard]] virtual bool is_recording() const = 0;

  /**
   * @brief Get current encoded frames
   * @return a copied vector of the encoded frames
   */
  virtual std::vector<EncodedVideoFrame> get_frames() const = 0;

  /**
   * @brief Clear current encoded frames
   */
  virtual void clear_frames() = 0;

  /**
   * @brief Get encoder context
   * @return AV Codec context
   */
  virtual AVCodecContext *get_encoder_context() const = 0;

  /**
   * @brief prepare frame before encoding (rotation wise)
   * @param src_data Screen frame before encoding
   * @param src_stride Screen frame source stride
   * @param out_stride Prepared frame out stride (for the encoder)
   * @return destenation data
   */
  virtual uint8_t *prepare_frame_data(uint8_t *src_data, int src_stride,
                                      int &out_stride) = 0;
};

/**
 * @brief Factory function to create platform-specific screen recorder
 * @return Unique pointer to platform-specific IScreenRecorder implementation
 */
std::unique_ptr<IScreenRecorder> create_screen_recorder();
} // namespace AVCapture::Platform
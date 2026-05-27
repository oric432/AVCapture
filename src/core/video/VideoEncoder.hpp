#pragma once

#include "VideoConfig.hpp"
#include "types.hpp"
#include "utils/error.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace AVCapture::Encoding {

/**
 * @brief Handles H.264 video encoding with hardware acceleration support
 */
class VideoEncoder {
public:
  static Error::Result<VideoEncoder> create(const Core::VideoConfig &config);

  ~VideoEncoder();

  VideoEncoder(const VideoEncoder &) = delete;
  VideoEncoder &operator=(const VideoEncoder &) = delete;

  VideoEncoder(VideoEncoder &&) noexcept;
  VideoEncoder &operator=(VideoEncoder &&) noexcept;

  Error::VoidResult initialize(const Core::VideoConfig &config);
  Error::VoidResult encode_frame(uint8_t *src_data, int src_line_size,
                                 int64_t pts, EncodedVideoFrame &encoded_frame);

  [[nodiscard]] AVCodecContext *get_codec_context() const { return codec_ctx_; }

  void cleanup();

private:
  VideoEncoder() = default;
  Error::VoidResult try_open_encoder(const char *encoder_name,
                                     const Core::VideoConfig &config);

  AVCodecContext *codec_ctx_ = nullptr;
  AVFrame *frame_ = nullptr;
  AVPacket *packet_ = nullptr;
  SwsContext *sws_ctx_ = nullptr;

  int height_ = 0;
};
} // namespace AVCapture::Encoding
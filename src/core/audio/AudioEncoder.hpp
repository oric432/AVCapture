#pragma once

#include "AudioConfig.hpp"
#include "types.hpp"
#include "utils/error.hpp"
#include <cstdint>


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

}

namespace AVCapture::Encoding {

// TODO integrate with CODECSKO library
class AudioEncoder {
public:
  static Error::Result<AudioEncoder> create(const Core::AudioConfig &config);

  ~AudioEncoder();

  AudioEncoder(const AudioEncoder &) = delete;
  AudioEncoder &operator=(const AudioEncoder &) = delete;

  AudioEncoder(AudioEncoder &&) noexcept;
  AudioEncoder &operator=(AudioEncoder &&) noexcept;

  Error::Result<bool> encode_frame(const int16_t *pcm_data,
                                   int32_t sample_count, int64_t pts,
                                   EncodedAudioFrame &out_frame);

  [[nodiscard]] AVCodecContext *get_codec_context() const { return codec_ctx_; }

private:
  AudioEncoder() = default;
  Error::VoidResult initialize(const Core::AudioConfig &config);
  void cleanup();
  Error::VoidResult drain();

  const AVCodec *codec_ = nullptr;
  AVCodecContext *codec_ctx_ = nullptr;
  SwrContext *swr_ctx_ = nullptr;

  Core::AudioConfig config_;
};
} // namespace AVCapture::Encoding

#include "AudioEncoder.hpp"
#include "AudioConfig.hpp"


using namespace AVCapture::Encoding;
using namespace AVCapture::Error;
using namespace AVCapture;

AudioEncoder::AudioEncoder(AudioEncoder &&other) noexcept {
  *this = std::move(other);
}

AudioEncoder &AudioEncoder::operator=(AudioEncoder &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  cleanup();

  codec_ = other.codec_;
  codec_ctx_ = other.codec_ctx_;
  swr_ctx_ = other.swr_ctx_;
  config_ = other.config_;

  other.codec_ = nullptr;
  other.codec_ctx_ = nullptr;
  other.swr_ctx_ = nullptr;

  return *this;
}

Result<AudioEncoder> AudioEncoder::create(const Core::AudioConfig &config) {
  AudioEncoder audio_encoder;

  if (auto res = audio_encoder.initialize(config); !res) {
    return std::unexpected(
        res.error().with_context("Failed initializing audio encoder"));
  }

  return audio_encoder;
}

AudioEncoder::~AudioEncoder() { cleanup(); }

VoidResult AudioEncoder::initialize(const Core::AudioConfig &config) {
  config_ = config;

  codec_ = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (codec_ == nullptr) {
    return std::unexpected(make_error().with_context("AAC codec not found"));
  }

  codec_ctx_ = avcodec_alloc_context3(codec_);
  if (codec_ctx_ == nullptr) {
    return std::unexpected(
        make_error().with_context("Could not allocate codec context"));
  }

  codec_ctx_->sample_rate = config_.sample_rate_;
  codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
  codec_ctx_->bit_rate = config_.bitrate_;
  codec_ctx_->frame_size = static_cast<int>(config_.buffer_frame_size_);

  if (config_.channels_ == 2) {
    av_channel_layout_default(&codec_ctx_->ch_layout, 2);
  } else {
    av_channel_layout_default(&codec_ctx_->ch_layout, 1);
  }

  codec_ctx_->time_base = {.num = 1, .den = config_.sample_rate_};

  if (avcodec_open2(codec_ctx_, codec_, nullptr) < 0) {
    cleanup();
    return std::unexpected(
        make_error().with_context("Failed to open audio codec"));
  }

  AVChannelLayout input_ch_layout;
  av_channel_layout_default(&input_ch_layout, config_.channels_);

  int ret = swr_alloc_set_opts2(&swr_ctx_, &codec_ctx_->ch_layout,
                                codec_ctx_->sample_fmt, codec_ctx_->sample_rate,
                                &input_ch_layout, AV_SAMPLE_FMT_S16,
                                codec_ctx_->sample_rate, 0, nullptr);

  av_channel_layout_uninit(&input_ch_layout);

  if (ret < 0 || swr_ctx_ == nullptr) {
    cleanup();
    return std::unexpected(
        make_error().with_context("Failed to allocated audio resampler"));
  }

  if (swr_init(swr_ctx_) < 0) {
    cleanup();
    return std::unexpected(
        make_error().with_context("Failed to initialize audio resampler"));
  }

  Log::audio_encode()->debug(
      "Audio encoder initialized: {}Hz, {} Channels, frame size={}",
      config_.sample_rate_, config_.channels_, codec_ctx_->frame_size);

  return {};
}

Result<bool> AudioEncoder::encode_frame(const int16_t *pcm_data,
                                        int32_t sample_count, int64_t pts,
                                        EncodedAudioFrame &out_frame) {
  if (codec_ctx_ == nullptr) {
    return std::unexpected(
        make_error().with_context("codec was not initialized"));
  }

  if (swr_ctx_ == nullptr) {
    return std::unexpected(
        make_error().with_context("swr was not initialized"));
  }

  auto *frame = av_frame_alloc();
  if (frame == nullptr) {
    return std::unexpected(
        make_error().with_context("Failed allocating frame"));
  }

  frame->format = codec_ctx_->sample_fmt;
  av_channel_layout_copy(&frame->ch_layout, &codec_ctx_->ch_layout);
  frame->sample_rate = codec_ctx_->sample_rate;
  frame->nb_samples = codec_ctx_->frame_size;

  if (av_frame_get_buffer(frame, 0) < 0) {
    cleanup();
    return std::unexpected(
        make_error().with_context("Failed to allocated audio frame buffer"));
  }

  const uint8_t *input[1] = {reinterpret_cast<const uint8_t *>(pcm_data)};

  int converted = swr_convert(swr_ctx_, frame->data, frame->nb_samples, input,
                              sample_count);

  if (converted < 0) {
    return std::unexpected(
        make_error().with_context("Audio resampling failed"));
  } else if (converted == 0) {
    return std::unexpected(
        make_error().with_context("Could not convert samples"));
  }

  frame->pts = pts;

  AVPacket *pkt = av_packet_alloc();
  if (pkt == nullptr) {
    return std::unexpected(
        make_error().with_context("ffmpeg packet was not initialized"));
  }

  int ret = avcodec_send_frame(codec_ctx_, frame);
  if (ret < 0) {
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return std::unexpected(
        make_error().with_context("Failed to send audio frame to encoder"));
  }

  ret = avcodec_receive_packet(codec_ctx_, pkt);
  if (ret == AVERROR(EAGAIN)) {
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return false;
  }

  if (ret == AVERROR_EOF) {
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return false;
  }

  else if (ret < 0) {
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return std::unexpected(
        make_error().with_context("Failed to recieve audio packet"));
  }

  out_frame.data_.assign(pkt->data, pkt->data + pkt->size);
  out_frame.pts_ = pkt->pts;
  out_frame.dts_ = pkt->dts;
  out_frame.is_key_frame_ = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

  av_packet_unref(pkt);
  av_packet_free(&pkt);
  av_frame_free(&frame);

  return true;
}

// Drain any remaining packets from the encoder to flush buffered data before
// freeing the context
VoidResult AudioEncoder::drain() {
  int ret = avcodec_send_frame(codec_ctx_, nullptr);

  if (ret < 0) {
    return std::unexpected(
        make_error().with_context("Failed sending nullptr frame to encoder"));
  }

  AVPacket *pkt = av_packet_alloc();
  if (pkt == nullptr) {
    return std::unexpected(
        make_error().with_context("Failed allocating new packet"));
  }

  for (;;) {
    ret = avcodec_receive_packet(codec_ctx_, pkt);

    if (ret == 0) {
      av_packet_unref(pkt);
      continue;
    }

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    }

    av_packet_free(&pkt);
    return std::unexpected(
        make_error().with_context("Failed receiving packet from encoder"));
  }

  av_packet_free(&pkt);
  return {};
}

void AudioEncoder::cleanup() {
  if (swr_ctx_ != nullptr) {
    swr_free(&swr_ctx_);
    swr_ctx_ = nullptr;
  }

  if (codec_ctx_ != nullptr) {
    if (auto res = drain(); !res) {
      Log::audio_encode()->warn("Failed flushing encoder: {}",
                                res.error().what());
    }
    avcodec_free_context(&codec_ctx_);
    codec_ctx_ = nullptr;
  }

  codec_ = nullptr;
}
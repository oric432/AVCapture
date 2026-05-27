
#include "VideoEncoder.hpp"
#include "VideoConfig.hpp"

using namespace AVCapture::Encoding;
using namespace AVCapture::Error;
using namespace AVCapture;

VideoEncoder::VideoEncoder(VideoEncoder &&other) noexcept {
  *this = std::move(other);
}

VideoEncoder &VideoEncoder::operator=(VideoEncoder &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  cleanup();

  codec_ctx_ = other.codec_ctx_;
  frame_ = other.frame_;
  packet_ = other.packet_;
  sws_ctx_ = other.sws_ctx_;
  height_ = other.height_;

  other.codec_ctx_ = nullptr;
  other.frame_ = nullptr;
  other.packet_ = nullptr;
  other.sws_ctx_ = nullptr;
  other.height_ = 0;

  return *this;
}

Result<VideoEncoder> VideoEncoder::create(const Core::VideoConfig &config) {
  VideoEncoder video_encoder;

  if (auto res = video_encoder.initialize(config); !res) {
    return std::unexpected(
        res.error().with_context("Failed initializing video encoder"));
  }

  return video_encoder;
}

VideoEncoder::~VideoEncoder() { cleanup(); }

VoidResult VideoEncoder::initialize(const Core::VideoConfig &config) {
  // Height is needed by sws_scale calls
  height_ = config.height_;

  // Try encoders in priority order
  const std::array<const char *, 4> encoders = {
      "h264_nvenc", // NVIDIA
      "h264_qsv",   // Intel QuickSync
      "h264_amf",   // AMD
      "libx264"     // Software fallback
  };

  for (const char *encoder : encoders) {
    if (auto res = try_open_encoder(encoder, config); res) {
      Log::video_encode()->debug("Using encoder: {}", encoder);

      // Allocates reusable frame we'll submit to the encoder
      frame_ = av_frame_alloc();
      if (frame_ == nullptr) {
        return std::unexpected(make_error().with_context(
            "Failed to allocate frame for video encoder"));
      }

      // The encoder dictates the output pixel format and coded size
      frame_->format = codec_ctx_->pix_fmt;
      frame_->width = codec_ctx_->width;
      frame_->height = codec_ctx_->height;

      // Allocate frame buffers with alignment (32 is safe for SIMD on most
      // platforms)
      if (av_frame_get_buffer(frame_, 32) < 0) {
        return std::unexpected(make_error().with_context(
            "Failed to allocate frame buffer for video encoder"));
      }

      // Allocate a reusable packet to receive encoded date from the encoder
      packet_ = av_packet_alloc();
      if (packet_ == nullptr) {
        return std::unexpected(make_error().with_context(
            "Failed to allocate packet for video encoder"));
      }

      // Initialize scaler for BGRA -> encoder_pix_fmt
      // Source: BGRA frames from screen capture
      // Dest:   codec->ctx_->pix_fmt chosen above
      // Filter: FAST_BILINEAR as screen content doesnt need fancy scaling
      sws_ctx_ =
          sws_getContext(config.width_, config.height_, AV_PIX_FMT_BGRA,
                         config.width_, config.height_, codec_ctx_->pix_fmt,
                         SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

      if (sws_ctx_ == nullptr) {
        return std::unexpected(make_error().with_context(
            "Failed to initialize scaler for video encoder"));
      }

      return {};
    } else {
      Log::video_encode()->debug(res.error().what());
    }
  }

  return std::unexpected(
      make_error().with_context("No H.264 encoder could be opened"));
}

VoidResult VideoEncoder::try_open_encoder(const char *encoder_name,
                                          const Core::VideoConfig &config) {
  const AVCodec *codec = avcodec_find_encoder_by_name(encoder_name);
  if (codec == nullptr) {
    return std::unexpected(make_error().with_context(
        std::format("Could not find encoder: {}", encoder_name)));
  }

  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
  }

  codec_ctx_ = avcodec_alloc_context3(codec);
  if (codec_ctx_ == nullptr) {
    return std::unexpected(make_error().with_context(std::format(
        "Could not allocate encoder context for {}", encoder_name)));
  }

  // Common parameters
  codec_ctx_->codec_id = AV_CODEC_ID_H264;
  codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_ctx_->width = config.width_;
  codec_ctx_->height = config.height_;

  // Timebase/Framerate pairing: TB=1/fps; r_frame_rate=fps/1 (Good for CFR
  // pipelines)
  codec_ctx_->time_base = AVRational{1, config.fps_};
  codec_ctx_->framerate = AVRational{config.fps_, 1};

  // One keyframe per second
  codec_ctx_->gop_size = config.fps_;

  // Target bitrate
  codec_ctx_->bit_rate = config.bitrate_;

  // Disable B-frames for low latency and simplicity
  codec_ctx_->max_b_frames = 0;

  // Default pixel format for software encoders
  codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;

  // Makes SPS/PPS appear in-band (ofter before keyframes) which helps with
  // segments that starts with P-frames So we avoid ffmpeg errors upon saving a
  // segment
  av_opt_set(codec_ctx_->priv_data, "repeat-headers", "1", 0);

  // Encoder-specific tuning
  if (strcmp(encoder_name, "h264_nvenc") == 0) {
    // NVIDIA_NVNEC
    av_opt_set(codec_ctx_->priv_data, "preset", "p1", 0);
    av_opt_set(codec_ctx_->priv_data, "rc", "cbr", 0);
    av_opt_set(codec_ctx_->priv_data, "rc-lookahead", "0", 0);
    av_opt_set(codec_ctx_->priv_data, "zerolatency", "1", 0);
  } else if (strcmp(encoder_name, "h264_qsv") == 0) {
    // Intel QuickSync prefers NV12 surfaces
    codec_ctx_->pix_fmt = AV_PIX_FMT_NV12;
    av_opt_set(codec_ctx_->priv_data, "preset", "veryfast", 0);
    av_opt_set(codec_ctx_->priv_data, "async_depth", "1", 0);
    av_opt_set(codec_ctx_->priv_data, "look_ahead", "0", 0);
    av_opt_set(codec_ctx_->priv_data, "low_power", "1", 0);
  } else if (strcmp(encoder_name, "h264_amf") == 0) {
    // AMD AMD
    av_opt_set(codec_ctx_->priv_data, "usage", "lowlatency", 0);
    av_opt_set(codec_ctx_->priv_data, "rc", "cbr", 0);
  } else if (strcmp(encoder_name, "libx264") == 0) {
    // Software x264
    av_opt_set(codec_ctx_->priv_data, "preset", "veryfast", 0);
    av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);
  }

  // Open encoder
  AVDictionary *opts = nullptr;
  int err = avcodec_open2(codec_ctx_, codec, &opts);
  if (opts != nullptr) {
    av_dict_free(&opts);
  }

  if (err < 0) {
    std::array<char, 256> err_buf{};
    av_strerror(err, err_buf.data(), err_buf.size());
    return std::unexpected(make_error().with_context(std::format(
        "Failed to open encoder {} : {}", encoder_name, err_buf.data())));
  }

  return {};
}

VoidResult VideoEncoder::encode_frame(uint8_t *src_data, int src_line_size,
                                      int64_t pts,
                                      EncodedVideoFrame &encoded_frame) {
  // Wrap the raw BGRA pointers into arrays, for sws_scale (expects per-plane
  // arrays)
  std::array<uint8_t *, 1> src = {src_data};
  std::array<int, 1> src_stride = {src_line_size};

  // Convert BGRA -> encoder pixel format into frame_ buffers
  // height_ controls how many rows we convert, must match the source height
  sws_scale(sws_ctx_, src.data(), src_stride.data(), 0, height_, frame_->data,
            frame_->linesize);

  // Assign PTS in the encoder's TimeBase
  frame_->pts = pts;

  // Submit the frame to the encoder (Queues work)
  int ret = avcodec_send_frame(codec_ctx_, frame_);
  if (ret < 0) {
    return std::unexpected(
        make_error().with_context("Failed sending frame to encoder context"));
  }

  // Try to receive a packet. not all calls produce one (pipelined encoders)
  ret = avcodec_receive_packet(codec_ctx_, packet_);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return std::unexpected(make_error().with_context(
        "Failed receiving packet from encoder context"));
  }

  if (ret < 0) {
    return std::unexpected(make_error().with_context(
        std::format("Error receiving encoded packet: {}", ret)));
  }

  // Copy out the encoded payload + timing flags for the caller to store/mux
  // later
  encoded_frame.data_.assign(packet_->data, packet_->data + packet_->size);
  encoded_frame.pts_ = packet_->pts;
  encoded_frame.dts_ = packet_->dts;
  encoded_frame.flags_ = packet_->flags;

  // Reset the packet so it can be reused on next call
  av_packet_unref(packet_);

  return {};
}

void VideoEncoder::cleanup() {
  // Free resources in proper order
  if (sws_ctx_ != nullptr) {
    sws_freeContext(sws_ctx_);
    sws_ctx_ = nullptr;
  }

  if (frame_ != nullptr) {
    av_frame_free(&frame_);
    frame_ = nullptr;
  }

  if (packet_ != nullptr) {
    av_packet_free(&packet_);
    packet_ = nullptr;
  }

  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
    codec_ctx_ = nullptr;
  }
}
#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <cstdint>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace json = boost::json;
using tcp = asio::ip::tcp;

namespace VSCapture {

/**
 * @brief Raw audio frame (PCM Data)
 */

struct RawAudioFrame {
  static constexpr size_t kMaxSamples = 1024;
  std::array<int16_t, kMaxSamples> data_{};
  size_t actual_size_;
  int64_t pts_{};

  explicit RawAudioFrame(const int16_t *source, size_t count, int64_t pts)
      : actual_size_(count), pts_(pts) {
    assert(count <= kMaxSamples && "Sample count exceed buffer size");
    std::memcpy(data_.data(), source, count * sizeof(int16_t));
  }

  boost::span<const int16_t> samples() const {
    return {data_.data(), actual_size_};
  }

  boost::span<int16_t> samples() { return {data_.data(), actual_size_}; }
};

/**
 * @brief Encoded audio frame (AAC Data)
 */

struct EncodedAudioFrame {
  std::vector<uint8_t> data_;
  int64_t pts_;
  int64_t dts_;
  bool is_key_frame_ = true;
};

/**
 * @brief Raw video frame to be encoded
 */
struct RawVideoFrame {
  std::vector<uint8_t> data_;
  int stride_{};
  int64_t pts_{};
  int width_{};
  int height_{};

  explicit RawVideoFrame(size_t size) : data_(size) {}
};

/**
 * @brief Structure to hold a single encoded ffmpeg frame
 */
struct EncodedVideoFrame {
  std::vector<uint8_t> data_;
  int64_t pts_;
  int64_t dts_;
  int flags_;
};

} // namespace VSCapture
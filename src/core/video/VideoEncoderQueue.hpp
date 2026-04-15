#pragma once

#include "utils/CircularBuffer.hpp"
#include "utils/readerwriterqueue.h"
#include "VideoConfig.hpp"
#include "VideoEncoder.hpp"
#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include "types.hpp"
#include "utils/error.hpp"

namespace VSCapture::Encoding {


/**
 * @brief Asynchronous video encoder using lock-free queue
 *
 * Separates frame capture from encoding to prevent blocking.
 * Uses a single producer, single consumer pattern.
 */
class VideoEncoderQueue {
public:
    VideoEncoderQueue();
    ~VideoEncoderQueue();

    /**
     * @brief Initialize the encoder queue
     * @param config Video configuration
     * @return true if successful
     */
    Error::VoidResult initialize(const Core::VideoConfig& config);

    /**
     * @brief Start the encoding thread
     */
    void start();

    /**
     * @brief Stop the encoding thread
     */
    void stop();

    /**
     * @brief Push a raw frame to the encoding queue
     * @param data Pointer to raw frame data
     * @param stride Frame stride in bytes
     * @param pts Presentation timestamp
     * @return true if frame was queued successfully
     */
    Error::VoidResult push_frame(const uint8_t* data, int stride, int64_t pts);

    /**
     * @brief Get all encoded frames
     * @return Vector of encoded frames
     */
    std::vector<EncodedVideoFrame> get_encoded_frames() const { return encoded_buffer_.snapshot(); }

    /**
     * @brief Clear all encoded frames
     */
    void clear_frames() { encoded_buffer_.clear(); }

    /**
     * @brief Get encoder context for muxing
     * @return Encoder codec context
     */
    AVCodecContext* get_encoder_context() const { return video_encoder_.has_value() ? video_encoder_->get_codec_context() : nullptr; }

    /**
     * @brief Get number of frames waiting to be encoded
     * @return Queue size
     */
    size_t get_queue_size() const;

private:
    /**
     * @brief Main encoding loop (runs in separate thread)
     */
    void encoding_loop(std::stop_token stop_token);

    Core::VideoConfig config_;
    std::optional<Encoding::VideoEncoder> video_encoder_;
    Utils::CircularBuffer<EncodedVideoFrame> encoded_buffer_;

    // Lock-free queue for raw frames
    std::unique_ptr<moodycamel::ReaderWriterQueue<std::unique_ptr<RawVideoFrame>>> frame_queue_;

    std::jthread encoding_thread_;
};

} // namespace VSCapture::Encoding
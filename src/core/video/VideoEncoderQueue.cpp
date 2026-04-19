
#include "VideoEncoderQueue.hpp"

using namespace VSCapture::Encoding;
using namespace VSCapture::Error;
using namespace VSCapture;
VideoEncoderQueue::VideoEncoderQueue()
    : frame_queue_(
          std::make_unique<moodycamel::ReaderWriterQueue<std::unique_ptr<RawVideoFrame>>>(
              100)) // Pre-allocate space for 1024 frames
{}

VideoEncoderQueue::~VideoEncoderQueue() {
    stop();
}

VoidResult VideoEncoderQueue::initialize(const Core::VideoConfig& config) {
    config_ = config;

    auto res = VideoEncoder::create(config_);

    // Initialize encoder
    if (!res) {
        return std::unexpected(res.error().with_context("Failed initializing video encoder"));
    }

    video_encoder_.emplace(std::move(res.value()));

    // Calculate buffer capacity
    const auto max_frames = static_cast<size_t>(config_.recording_length_seconds_ * config_.fps_);

    encoded_buffer_.set_capacity(max_frames);

    Log::video_encode()->debug(
        "Video encoder queue configured: {:.1f}s duration, {} frames "
        "({}fps, {}x{})",
        config_.recording_length_seconds_,
        max_frames,
        config_.fps_,
        config_.width_,
        config_.height_);

    return {};
}

void VideoEncoderQueue::start() {
    if (encoding_thread_.joinable()) {
        return;
    }

    encoding_thread_ =  std::jthread([this](std::stop_token stop_token) {
        encoding_loop(stop_token);
    });
    Log::video_encode()->info("Video encoding thread started");
}

void VideoEncoderQueue::stop() {
    if (!encoding_thread_.joinable()) {
        return;
    }

    encoding_thread_.request_stop();
    encoding_thread_.join();

    encoded_buffer_.clear();
    Log::video_encode()->debug("Video encoding thread stopped");
}

VoidResult VideoEncoderQueue::push_frame(const uint8_t* data, int stride, int64_t pts) {
    // Calculate frame size
    const size_t frame_size = stride * config_.height_;

    // Create a raw frame
    auto frame = std::make_unique<RawVideoFrame>(frame_size);

    // Copy frame data
    std::memcpy(frame->data_.data(), data, frame_size);
    frame->stride_ = stride;
    frame->pts_ = pts;
    frame->width_ = config_.width_;
    frame->height_ = config_.height_;

    // Try to enqueue (non-blocking)
    if (frame_queue_->try_enqueue(std::move(frame))) {
        return {};
    }

    // Queue is full, frame dropped
    return std::unexpected(make_error().with_context(std::format("Frame queue full, dropping frame with PTS {}", pts)));
}

size_t VideoEncoderQueue::get_queue_size() const {
    return frame_queue_->size_approx();
}

void VideoEncoderQueue::encoding_loop(std::stop_token stop_token) {
    std::unique_ptr<RawVideoFrame> frame;

    while (!stop_token.stop_requested()) {
        // Try to dequeue a frame (non-blocking)
        if (frame_queue_->try_dequeue(frame)) {
            // Encode the frame
            EncodedVideoFrame encoded_frame{};

            if (auto res = video_encoder_->encode_frame(frame->data_.data(), frame->stride_, frame->pts_, encoded_frame);
                !res) {
                Log::video_encode()->error("Failed to encode frame with PTS {} - {}", frame->pts_, res.error().what());
            }
            else {
                // Push encoded frame to buffer
                encoded_buffer_.push(std::move(encoded_frame));
            }
        }
        else {
            // No frames available, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Process remaining frames in queue
    Log::video_encode()->info("Processing remaining frames in queue...");
    while (frame_queue_->try_dequeue(frame)) {
        EncodedVideoFrame encoded_frame{};

        if (auto res = video_encoder_->encode_frame(frame->data_.data(), frame->stride_, frame->pts_, encoded_frame); !res) {
            Log::video_encode()->error("Failed to encode frame with PTS {} - {}", frame->pts_, res.error().what());
        }
        else {
            // Push encoded frame to buffer
            encoded_buffer_.push(std::move(encoded_frame));
        }
    }

    Log::video_encode()->debug("Encoding loop finished");
}
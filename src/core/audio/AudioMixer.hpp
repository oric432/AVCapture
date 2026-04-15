#pragma once

#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>
#include "AudioConfig.hpp"
#include "AudioEncoder.hpp"
#include "utils/CircularBuffer.hpp"
#include "utils/readerwriterqueue.h"
#include "types.hpp"
#include "boost/core/span.hpp"
#include "utils/error.hpp"
#include <optional>

namespace VSCapture::Encoding {

class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    Error::VoidResult initialize(const Core::AudioConfig& config);

    void start();
    void stop();

    Error::VoidResult add_stream(const unsigned int device_id);
    Error::VoidResult remove_stream(const unsigned int device_id);

    Error::VoidResult push_audio(const unsigned int device_id, std::unique_ptr<RawAudioFrame> frame);

    std::vector<EncodedAudioFrame> get_audio_buffer() const { return audio_buffer_.snapshot(); }
    void clear_frames() { audio_buffer_.clear(); }
    AVCodecContext* get_codec_context() const { return audio_encoder_.has_value() ? audio_encoder_->get_codec_context() : nullptr; }

private:
    using AudioStream = moodycamel::ReaderWriterQueue<std::unique_ptr<RawAudioFrame>>;

    void wait_for_all_stream(std::stop_token stop_token);
    void mixer_loop(std::stop_token stop_token);

    static void mix_samples(std::vector<int16_t>& output, const boost::span<const int16_t>& input);
    void apply_soft_clipping(std::vector<int16_t>& buffer);

    std::unordered_map<unsigned int, std::unique_ptr<AudioStream>> audio_streams_;

    Core::AudioConfig config_;

    std::optional<AudioEncoder> audio_encoder_;

    Utils::CircularBuffer<EncodedAudioFrame> audio_buffer_;
    int64_t pts_{0};

    std::jthread mixer_thread_;
};
} // namespace VSCapture::Encoding
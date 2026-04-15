
#include "AudioConfig.hpp"
#include "AudioMixer.hpp"

using namespace VSCapture::Encoding;
using namespace VSCapture::Error;
using namespace VSCapture;


AudioMixer::AudioMixer() = default;

AudioMixer::~AudioMixer() {
    stop();
}

VoidResult AudioMixer::initialize(const Core::AudioConfig& config) {
    config_ = config;
    auto res = AudioEncoder::create(config_);

    if (!res) {
        return std::unexpected(res.error().with_context("Failed to initialize audio encoder"));
    }

    audio_encoder_.emplace(std::move(res.value()));

    const auto total_sample_target =
        static_cast<int64_t>(std::llround(config_.buffer_duration_ * config_.sample_rate_));

    const auto max_frames =
        static_cast<size_t>((total_sample_target + config_.buffer_frame_size_ - 1) / config_.buffer_frame_size_);

    audio_buffer_.set_capacity(max_frames);

    const double actual_duration =
        static_cast<double>(max_frames * config_.buffer_frame_size_) / static_cast<double>(config_.sample_rate_);

    Log::audio_capture()->debug(
        "Audio buffer configured: {:.1f}s duration, {} frames ({}HHz, "
        "{} frame size)",
        actual_duration,
        max_frames,
        config_.sample_rate_,
        config_.buffer_frame_size_);

    return {};
}

void AudioMixer::start() {
    if (mixer_thread_.joinable()) {
        return;
    }

    mixer_thread_ = std::jthread([this](std::stop_token stop_token) { mixer_loop(stop_token); });
}
void AudioMixer::stop() {
    mixer_thread_ = {};  // request_stop() + join() via jthread destructor
    audio_buffer_.clear();
    Log::audio_encode()->debug("Audio encoding thread stopped");
}

VoidResult AudioMixer::add_stream(const unsigned int device_id) {
    if (audio_streams_.find(device_id) != audio_streams_.end()) {
        return std::unexpected(make_error().with_context("Audio stream already exists"));
    }

    audio_streams_[device_id] = std::make_unique<AudioStream>();

    return {};
}

VoidResult AudioMixer::remove_stream(const unsigned int device_id) {
    auto stream = audio_streams_.find(device_id);
    if (stream == audio_streams_.end()) {
        return std::unexpected(make_error().with_context("Audio stream not found"));
    }

    audio_streams_.erase(stream);

    return {};
}

// should have used a queue directly but it works fine for now
VoidResult AudioMixer::push_audio(const unsigned int device_id, std::unique_ptr<RawAudioFrame> frame) {
    auto stream = audio_streams_.find(device_id);
    if (stream == audio_streams_.end()) {
        return std::unexpected(make_error().with_context("Audio stream already exists"));
    }

    if (!stream->second->enqueue(std::move(frame))) {
        return std::unexpected(make_error().with_context("Queue is full"));
    }

    return {};
}

// Change stream names to queue
void AudioMixer::wait_for_all_stream(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        bool all_ready = true;

        for (auto& [device_id, stream] : audio_streams_) {
            auto* peek = stream->peek();
            if (peek == nullptr) {
                all_ready = false;
                break;
            }
        }

        if (all_ready) {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void AudioMixer::mixer_loop(std::stop_token stop_token) {
    std::vector<int16_t> mixed_buffer(config_.buffer_frame_size_ * config_.channels_);

    // TODO Can i use a vector
    std::unordered_map<unsigned int, std::unique_ptr<RawAudioFrame>> current_frames;

    while (!stop_token.stop_requested()) {
        wait_for_all_stream(stop_token);

        current_frames.clear();
        for (auto& [device_id, stream] : audio_streams_) {
            std::unique_ptr<RawAudioFrame> audio_frame;
            if (stream->try_dequeue(audio_frame)) {
                current_frames[device_id] = std::move(audio_frame);
            }
        }

        std::fill(mixed_buffer.begin(), mixed_buffer.end(), 0);

        for (auto& pair : audio_streams_) {
            auto device_id = pair.first;

            auto frame = current_frames.find(device_id);
            if (frame == current_frames.end() || !frame->second) {
                continue;
            }

            mix_samples(mixed_buffer, frame->second->samples());
        }

        EncodedAudioFrame encoded_frame;
        auto res = audio_encoder_->encode_frame(
            mixed_buffer.data(),
            static_cast<int32_t>(mixed_buffer.size()),
            pts_,
            encoded_frame);
        if (!res) {
            Log::audio_encode()->warn("Failed encoding audio frame: {}", res.error().what());
        }
        // Check if value true or false, if false = AVERROR / AVEAGAIN, no need to push new packet
        else if (res.value()) {
            pts_ += static_cast<int64_t>(mixed_buffer.size());
            audio_buffer_.push(std::move(encoded_frame));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void AudioMixer::mix_samples(std::vector<int16_t>& output, const boost::span<const int16_t>& input) {
    size_t samples_to_mix = std::min(output.size(), input.size());

    for (size_t i = 0; i < samples_to_mix; ++i) {
        using LargerSampleType = int32_t; // To avoid overflow.

        // TODO Improve mixing algo for more qualititive results.
        const LargerSampleType mixed_sample = output[i] + input[i];

        static constexpr LargerSampleType kMIN_SAMPLE = std::numeric_limits<int16_t>::min();
        static constexpr LargerSampleType kMAX_SAMPLE = std::numeric_limits<int16_t>::max();
        const auto clipped_sample = std::max<int32_t>(kMIN_SAMPLE, std::min<int32_t>(mixed_sample, kMAX_SAMPLE));

        output[i] = static_cast<int16_t>(clipped_sample);
    }
}

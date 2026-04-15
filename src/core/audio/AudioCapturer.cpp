#include "AudioConfig.hpp"

#include "AudioCapturer.hpp"
#include "RtAudio.h"
#include "utils/error.hpp"

using namespace VSCapture::Core;
using namespace VSCapture::Error;
using namespace VSCapture;


AudioCapturer::AudioCapturer()
    : system_audio_(RtAudio::UNSPECIFIED, &rt_audio_callback)
    , mic_audio_(RtAudio::UNSPECIFIED, &rt_audio_callback) {}


AudioCapturer::~AudioCapturer() {
    stop();
}

VoidResult AudioCapturer::initialize(const Core::AudioConfig& config) {
    if (is_initialized_) {
        return std::unexpected(make_error().with_context("Failed to initialize since object is already initialized"));
    }

    config_ = config;

    if (auto res = audio_mixer_.initialize(config_); !res) {
        return std::unexpected(res.error().with_context("Failed to initialize audio mixer"));
    }

    system_device_id_ = find_loopback_device();
    if (system_device_id_ < 0) {
        Log::audio_capture()->warn("No loopback device found");
    }
    else {
        system_audio_.setErrorCallback([](RtAudioErrorType /**/, const std::string& /**/) { return; });

        if (auto res = audio_mixer_.add_stream(system_device_id_); !res) {
            Log::audio_capture()->warn("Failed adding stream to mixer: {}", res.error().what());
        }
    }

    mic_device_id_ = find_microphone_device();
    if (mic_device_id_ < 0) {
        Log::audio_capture()->warn("No microphone device found");
    }
    else {
        mic_audio_.setErrorCallback([](RtAudioErrorType /**/, const std::string& /**/) { return; });

        if (auto res = audio_mixer_.add_stream(mic_device_id_); !res) {
            Log::audio_capture()->warn("Failed adding stream to mixer: {}", res.error().what());
        }
    }

    if (system_device_id_ < 0 && mic_device_id_ < 0) {
        return std::unexpected(make_error().with_context("Neither input and output devices were found"));
    }

    is_initialized_ = true;
    return {};
}

VoidResult AudioCapturer::start() {
    if (!is_initialized_) {
        return std::unexpected(make_error().with_context("Failed to start since object is not initialized"));
    }

    if (is_capturing_) {
        return std::unexpected(make_error().with_context("Failed to start since capture threads are already running"));
    }

    RtAudio::StreamParameters params;
    params.nChannels = config_.channels_;
    params.firstChannel = 0;

    if (system_device_id_ >= 0) {
        params.deviceId = system_device_id_;

        if (system_audio_.openStream(
                nullptr,
                &params,
                RTAUDIO_SINT16,
                config_.sample_rate_,
                &config_.buffer_frame_size_,
                &AudioCapturer::system_audio_callback,
                this) != 0) {
            return std::unexpected(make_error().with_context("Failed to open system audio"));
        }

        if (system_audio_.startStream() != 0) {
            return std::unexpected(make_error().with_context("Failed starting system capture"));
        }
    }

    if (mic_device_id_ >= 0 && system_device_id_ != mic_device_id_) {
        params.deviceId = mic_device_id_;

        if (mic_audio_.openStream(
                nullptr,
                &params,
                RTAUDIO_SINT16,
                config_.sample_rate_,
                &config_.buffer_frame_size_,
                &AudioCapturer::mic_audio_callback,
                this) != 0) {
            return std::unexpected(make_error().with_context("Failed to open mic audio"));
        }

        if (mic_audio_.startStream() != 0) {
            return std::unexpected(make_error().with_context("Failed starting mic capture"));
        }
    }

    audio_mixer_.start();

    is_capturing_ = true;

    return {};
}

void AudioCapturer::stop() {
    if (!is_capturing_) {
        return;
    }

    // Stop and close system audio stream
    if (system_audio_.isStreamOpen()) {
        if (system_audio_.isStreamRunning()) {
            if (system_audio_.stopStream() != 0) {
                Log::audio_capture()->error("Failed to stop system audio stream");
            }
        }

        system_audio_.closeStream();
    }

    // Stop and close mic audio stream
    if (mic_audio_.isStreamOpen()) {
        if (mic_audio_.isStreamRunning()) {
            if (mic_audio_.stopStream() != 0) {
                Log::audio_capture()->error("Failed to stop mic audio stream");
            }
        }

        mic_audio_.closeStream();
    }

    audio_mixer_.stop();

    is_capturing_ = false;
    Log::audio_capture()->info("Audio capture stopped. Total sampled captured: {}", total_samples_);
}
bool AudioCapturer::is_capturing() {
    return is_capturing_;
}

int AudioCapturer::system_audio_callback(
    void* /*output_buffer*/,
    void* input_buffer,
    unsigned int n_frames,
    double /*stream_time*/,
    RtAudioStreamStatus status,
    void* user_data) {
    auto* capturer = static_cast<AudioCapturer*>(user_data);

    if (!capturer->is_capturing()) {
        return 0;
    }

    if (status != 0U) {
        Log::audio_capture()->warn("Audio stream overflow/underflow detected");
    }

    if (input_buffer != nullptr) {
        const auto* data = reinterpret_cast<const int16_t*>(input_buffer);
        const auto samples_count = static_cast<size_t>(n_frames);

        auto raw_audio_frame = std::make_unique<RawAudioFrame>(data, samples_count, 0);

        if (auto res = capturer->audio_mixer_.push_audio(capturer->system_device_id_, std::move(raw_audio_frame));
            res) {
            Log::audio_capture()->trace("Pushed {} system frames successfully", n_frames);
        }
        else {
            Log::audio_capture()->warn("Could not push audio to queue: {}", res.error().what());
        }
    }

    return 0;
}

int AudioCapturer::mic_audio_callback(
    void* /*output_buffer*/,
    void* input_buffer,
    unsigned int n_frames,
    double /*stream_time*/,
    RtAudioStreamStatus status,
    void* user_data) {
    auto* capturer = static_cast<AudioCapturer*>(user_data);

    if (!capturer->is_capturing()) {
        return 0;
    }

    if (status != 0U) {
        Log::audio_capture()->warn("Audio stream overflow/underflow detected");
    }

    if (input_buffer != nullptr) {
        const auto* data = reinterpret_cast<const int16_t*>(input_buffer);
        const auto samples_count = static_cast<size_t>(n_frames);

        auto raw_audio_frame = std::make_unique<RawAudioFrame>(data, samples_count, 0);

        if (auto res = capturer->audio_mixer_.push_audio(capturer->mic_device_id_, std::move(raw_audio_frame)); res) {
            Log::audio_capture()->trace("Pushed {} mic frames successfully", n_frames);
        }
        else {
            Log::audio_capture()->warn("Could not push audio to queue: {}", res.error().what());
        }
    }

    return 0;
}

int AudioCapturer::find_loopback_device() {
    auto device_ids = system_audio_.getDeviceIds();

    for (auto device_id : device_ids) {
        RtAudio::DeviceInfo info = system_audio_.getDeviceInfo(device_id);

        if (info.name.find(config_.output_device_name_) != std::string::npos) {
            return static_cast<int>(device_id);
        }
    }

    auto default_output = system_audio_.getDefaultOutputDevice();
    if (default_output >= 0) {
        RtAudio::DeviceInfo info = system_audio_.getDeviceInfo(default_output);
        Log::audio_capture()->warn("No specific loopback device found, using default input device: {}", info.name);
        return static_cast<int>(default_output);
    }

    return -1;
}

int AudioCapturer::find_microphone_device() {
    auto device_ids = mic_audio_.getDeviceIds();

    for (auto device_id : device_ids) {
        RtAudio::DeviceInfo info = mic_audio_.getDeviceInfo(device_id);

        if (info.inputChannels == 0) {
            continue;
        }

        if (info.name.find(config_.input_device_name_) != std::string::npos) {
            return static_cast<int>(device_id);
        }
    }

    auto default_input = mic_audio_.getDefaultInputDevice();
    if (default_input >= 0) {
        RtAudio::DeviceInfo info = mic_audio_.getDeviceInfo(default_input);
        if (info.inputChannels > 0) {
            Log::audio_capture()->warn("No specific loopback device found, using default input device: {}", info.name);
            return static_cast<int>(default_input);
        }
    }

    return -1;
}
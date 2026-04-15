#pragma once


#include <atomic>
#include <cstdint>
#include "AudioConfig.hpp"
#include "AudioMixer.hpp"
#include "RtAudio.h"
#include "types.hpp"
#include "rtaudio_c.h"

namespace VSCapture::Core {

class AudioCapturer {
public:
    AudioCapturer();
    ~AudioCapturer();

    Error::VoidResult initialize(const Core::AudioConfig& config);
    Error::VoidResult start();
    void stop();
    bool is_capturing();

    std::vector<EncodedAudioFrame> get_frames() const { return audio_mixer_.get_audio_buffer(); }
    void clear_frames() { audio_mixer_.clear_frames(); }
    AVCodecContext* get_encoder_context() const { return audio_mixer_.get_codec_context(); }

private:
    static void rt_audio_callback(RtAudioErrorType /**/, const std::string& /**/) {};

    static int system_audio_callback(
        void* output_buffer,
        void* input_buffer,
        unsigned int n_frames,
        double stream_time,
        RtAudioStreamStatus status,
        void* user_data);

    static int mic_audio_callback(
        void* output_buffer,
        void* input_buffer,
        unsigned int n_frames,
        double stream_time,
        RtAudioStreamStatus status,
        void* user_data);

    int find_loopback_device();

    int find_microphone_device();

    RtAudio system_audio_;
    RtAudio mic_audio_;

    Core::AudioConfig config_;

    // Captured frames buffer
    Encoding::AudioMixer audio_mixer_;

    std::atomic<bool> is_capturing_{false}; // For system audio (loopback/WASAPI/monitor)
    std::atomic<bool> is_initialized_{false}; // For microphone input

    // For audio timing
    int64_t total_samples_ = 0;

    // Device IDs
    int system_device_id_ = -1;
    int mic_device_id_ = -1;
};
} // namespace VSCapture::Core
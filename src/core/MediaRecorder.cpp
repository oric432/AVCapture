
#include "export/ArtifactExporter.hpp"
#include "audio/AudioCapturer.hpp"
#include "audio/AudioConfig.hpp"
#include "RollingSegments.hpp"
#include "video/VideoConfig.hpp"
#include "MediaRecorder.hpp"

using namespace VSCapture::Core;
using namespace VSCapture::Error;
using namespace VSCapture;

MediaRecorder::MediaRecorder() = default;

MediaRecorder::~MediaRecorder() {
    stop();
}

VoidResult MediaRecorder::initialize(Platform::RecordingConfig& recorder_config) {
    recorder_config_ = recorder_config;

    if (recorder_config_.role_type_ == RoleType::kVideo || recorder_config_.role_type_ == RoleType::kNone) {
        auto screen_recorder = Platform::create_screen_recorder();
        if (!screen_recorder) {
            Log::crash_error("Could not create screen recording");
        }

        screen_recorder_ = std::move(screen_recorder);

        VideoConfig video_config;

        video_config.fps_ = recorder_config.video.fps_;
        video_config.bitrate_ = recorder_config.video.bitrate_;
        video_config.monitor_index_ = recorder_config.video.monitor_index_;
        video_config.recording_length_seconds_ = recorder_config.video.segment_seconds_;

        if (auto res = screen_recorder_->initialize(video_config); !res) {
            return std::unexpected(res.error().with_context("Failed to initialize screen recorder"));
        }
    }

    if (recorder_config_.role_type_ == RoleType::kAudio || recorder_config_.role_type_ == RoleType::kNone) {
        audio_capturer_.emplace();

        AudioConfig audio_config;

        audio_config.sample_rate_ = recorder_config.audio.sample_rate_;
        audio_config.channels_ = recorder_config.audio.channels_;
        audio_config.bitrate_ = recorder_config.audio.bitrate_;
        audio_config.recording_length_seconds_ = recorder_config.video.segment_seconds_;
        audio_config.buffer_frame_size_ = recorder_config.audio.buffer_frame_size_;
        audio_config.output_device_name_ = recorder_config.audio.output_device_name_;
        audio_config.input_device_name_ = recorder_config.audio.input_device_name_;

        if (auto res = audio_capturer_->initialize(audio_config); !res) {
            return std::unexpected(res.error().with_context("Failed to initialize audio capturer"));
        }
    }


    Core::RollingSegment::Config segment_config;
    switch (recorder_config.role_type_) {
    case RoleType::kAudio:
        segment_config.dir_ = std::filesystem::temp_directory_path() / "VSCapture" / "audio_ring";
        break;
    case RoleType::kVideo:
        segment_config.dir_ = std::filesystem::temp_directory_path() / "VSCapture" / "video_ring";
        break;
    default: segment_config.dir_ = std::filesystem::temp_directory_path() / "VSCapture" / "ring"; break;
    }

    segment_config.role_type = recorder_config.role_type_;
    segment_config.segment_seconds_ = static_cast<int>(recorder_config.video.segment_seconds_);
    segment_config.ring_size_ =
        static_cast<size_t>(recorder_config.video.recording_length_seconds_ / recorder_config.video.segment_seconds_);

    if (auto res = segmenter_.initialize(
            segment_config,
            screen_recorder_ != nullptr ? screen_recorder_.get() : nullptr,
            audio_capturer_ ? &*audio_capturer_ : nullptr);
        !res) {
        return std::unexpected(res.error().with_context("Failed to initialize rolling segmenter"));
    }

    nfs_client_ = std::make_unique<Nfs::Client>();
    exporter_ = std::make_unique<ArtifactExporter>(recorder_config_);

    return {};
}
VoidResult MediaRecorder::start() {
    if (auto res = segmenter_.start(); !res) {
        return std::unexpected(res.error().with_context("Failed starting rolling segmenter"));
    }

    if (screen_recorder_) {
        if (auto res = screen_recorder_->start(); !res) {
            return std::unexpected(res.error().with_context("Failed starting screen recorder"));
        }
    }

    if (audio_capturer_) {
        if (auto res = audio_capturer_->start(); !res) {
            return std::unexpected(res.error().with_context("Failed starting audio capturer"));
        }
    }

    return {};
}

void MediaRecorder::stop() {
    segmenter_.stop();

    if (audio_capturer_) {
        audio_capturer_->stop();
    }

    if (screen_recorder_) {
        screen_recorder_->stop();
    }
}

bool MediaRecorder::is_recording() {
    return (audio_capturer_ && audio_capturer_->is_capturing()) ||
           (screen_recorder_ != nullptr && screen_recorder_->is_recording());
}

Result<std::string> MediaRecorder::save_and_upload(std::string_view id, std::string_view ip) {
    auto res = exporter_->save_and_upload(segmenter_, *nfs_client_, id, ip);
    if (!res) {
        return std::unexpected(res.error().with_context("Failed saving and uploading"));
    }

    Log::media_recorder()->info("Saved artifact to: {}", res.value());
    return res.value();
}

Result<std::string> MediaRecorder::save_and_upload_async(std::string id, std::string ip) {
    bool expected = false;
    if (!save_in_progress_.compare_exchange_strong(expected, true)) {
        return std::unexpected(make_error().with_context("Save already in progress"));
    }

    if (save_thread_.joinable()) {
        save_thread_.join();
    }

    auto bundle = exporter_->prepare(id, ip);
    if (!bundle) {
        save_in_progress_.store(false, std::memory_order_release);
        return std::unexpected(bundle.error().with_context("Failed preparing bundle"));
    }

    std::string name = bundle->remote_final_;

    save_thread_ = std::jthread([this, b = std::move(bundle.value())]() mutable {
        if (auto res = exporter_->execute(segmenter_, *nfs_client_, b); !res) {
            Log::media_recorder()->error("Failed saving recording: {}", res.error().what());
        } else {
            Log::media_recorder()->info("Saved recording and logs to: {}", b.remote_final_);
        }
        save_in_progress_.store(false, std::memory_order_release);
    });

    return name;
}
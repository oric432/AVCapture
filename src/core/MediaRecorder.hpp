#pragma once

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include "audio/AudioCapturer.hpp"
#include "types.hpp"
#include "video/IScreenRecorder.hpp"
#include "storage/NfsClient.hpp"
#include "RollingSegments.hpp"
#include "video/VideoConfig.hpp"
#include "utils/error.hpp"

namespace VSCapture::Core { class ArtifactExporter; }

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
#include <libavformat/avio.h>
#include <libavutil/rational.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

namespace VSCapture::Core {

class MediaRecorder {
public:
    MediaRecorder();
    ~MediaRecorder();

    Error::VoidResult initialize(Platform::RecordingConfig& recorder_config);
    Error::VoidResult start();
    void stop();
    bool is_recording();

    RoleType role_type() const { return recorder_config_.role_type_; }

    Error::Result<std::string> save_and_upload(std::string_view id = "");
    Error::Result<std::string> save_and_upload_async(std::string id = "");

private:
    std::unique_ptr<Platform::IScreenRecorder> screen_recorder_;
    std::unique_ptr<Nfs::IFileBackend> nfs_client_;
    std::unique_ptr<ArtifactExporter> exporter_;
    Core::RollingSegment segmenter_;
    std::optional<Core::AudioCapturer> audio_capturer_;

    std::atomic<bool> save_in_progress_{false};
    std::jthread save_thread_;

    Platform::RecordingConfig recorder_config_;
};
} // namespace VSCapture::Core
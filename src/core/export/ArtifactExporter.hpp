#pragma once

#include <filesystem>
#include <string>
#include "utils/error.hpp"
#include "core/storage/IFileBackend.hpp"
#include "core/video/IScreenRecorder.hpp"
#include "core/RollingSegments.hpp"

namespace VSCapture::Core {
class ArtifactExporter {
public:
    explicit ArtifactExporter(Platform::RecordingConfig& cfg);
    ~ArtifactExporter();

    Error::Result<std::string>
    save_and_upload(RollingSegment& segmenter, Nfs::IFileBackend& nfs);

private:
    struct BundlePaths {
        std::filesystem::path bundle_dir_;
        std::filesystem::path output_mp4_;
        std::filesystem::path zip_path_;
        std::string remote_final_;
        std::string remote_tmp_;
        size_t segments_to_export_;
    };

    Error::Result<BundlePaths> prepare() const;
    Error::VoidResult export_mp4(RollingSegment& segmenter, const BundlePaths& bundle_paths) const;
    Error::VoidResult zip_bundle(const BundlePaths& bundle_paths) const;
    Error::VoidResult upload(Nfs::IFileBackend& nfs, const BundlePaths& bundle_paths) const;
    void cleanup(const BundlePaths& bundle_paths) const;

    size_t get_number_of_segments() const {
        return static_cast<size_t>(config_.video.recording_length_seconds_ / config_.video.segment_seconds_);
    }

    Platform::RecordingConfig config_;

    std::atomic<bool> save_in_progress_{false};
    std::thread save_thread_;
};


} // namespace VSCapture::Core
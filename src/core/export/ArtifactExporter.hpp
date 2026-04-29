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

    struct BundlePaths {
        std::filesystem::path bundle_dir_;
        std::filesystem::path output_mp4_;
        std::filesystem::path zip_path_;
        std::string ip_dir_;       // IP used as NFS subdirectory (mkdir_p target)
        std::string remote_final_; // "{ip}/{filename}.zip" — NFS path and API display
        std::string remote_tmp_;
        size_t segments_to_export_;
    };

    Error::Result<std::string>
    save_and_upload(RollingSegment& segmenter, Nfs::IFileBackend& nfs, std::string_view id = "", std::string_view ip = "");

    Error::Result<BundlePaths> prepare(std::string_view id = "", std::string_view ip = "") const;
    Error::VoidResult execute(RollingSegment& segmenter, Nfs::IFileBackend& nfs, const BundlePaths& bundle_paths);

private:
    Error::VoidResult export_mp4(RollingSegment& segmenter, const BundlePaths& bundle_paths) const;
    Error::VoidResult zip_bundle(const BundlePaths& bundle_paths) const;
    Error::VoidResult upload(Nfs::IFileBackend& nfs, const BundlePaths& bundle_paths) const;
    void cleanup(const BundlePaths& bundle_paths) const;

    size_t get_number_of_segments() const {
        return static_cast<size_t>(config_.video.recording_length_seconds_ / config_.video.segment_seconds_);
    }

    Platform::RecordingConfig config_;
};


} // namespace VSCapture::Core
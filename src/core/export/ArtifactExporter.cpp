#include "pch.h"

#include "ArtifactExporter.hpp"
#include "core/export/VSLogs.hpp"
#include "core/video/IScreenRecorder.hpp"

using namespace VSCapture::Core;
using namespace VSCapture::Error;
using namespace VSCapture;

ArtifactExporter::ArtifactExporter(Platform::RecordingConfig &cfg)
    : config_(cfg) {}

ArtifactExporter::~ArtifactExporter() {
  if (save_thread_.joinable()) {
    save_thread_.join();
  }
}

Result<std::string> ArtifactExporter::save_and_upload(RollingSegment &segmenter,
                                                      Nfs::IFileBackend &nfs) {
  auto bundle = prepare();
  if (!bundle) {
    return std::unexpected(
        bundle.error().with_context("Failed creating bundle paths"));
  }

  if (auto res = export_mp4(segmenter, bundle.value()); !res) {
    return std::unexpected(res.error().with_context("Failed exporting mp4"));
  }

  if (auto res = zip_bundle(bundle.value()); !res) {
    return std::unexpected(res.error().with_context("Failed zipping bundle"));
  }

  if (!config_.nfs.save_locally_) {
    if (auto res = upload(nfs, bundle.value()); !res) {
      return std::unexpected(
          res.error().with_context("Failed uploading to nfs"));
    }
  }

  cleanup(bundle.value());
  return bundle->remote_final_;
}

Result<ArtifactExporter::BundlePaths> ArtifactExporter::prepare() const {
  auto segments = get_number_of_segments();
  std::filesystem::path tmp_path;

  if (config_.nfs.save_locally_) {
    tmp_path = std::filesystem::current_path();
  } else {
    tmp_path = std::filesystem::temp_directory_path() / "VSCapture";
  }

  std::filesystem::path log_path{};

  if (config_.vs.type_ == VsType::kVS) {
    if (!config_.vs.log_path_.empty()) {
      log_path = config_.vs.log_path_;
    } else {
      auto ver = VSCapture::VSLogs::read_app_version(
          std::format("{}/client_configuration.json", config_.vs.app_path_));
      if (!ver) {
        Log::media_recorder()->warn(
            "Failed finding version at: {}",
            std::format("{}/client_configuration.json", config_.vs.app_path_));
      } else {
        log_path = std::format("{}/versions/vs-{}/logs", config_.vs.app_path_,
                               ver.value());
      }
    }
  } else if (config_.vs.type_ == VsType::kSoft) {
    log_path = config_.vs.app_path_;
    log_path /= "ThinVsLogs";
  }

  auto bundle_dir =
      VSLogs::build_bundle_folder(tmp_path, log_path, config_.nfs.save_locally_,
                                  config_.vs.type_, config_.role_type_);
  if (!bundle_dir) {
    return std::unexpected(
        bundle_dir.error().with_context("Failed creating bundle dir"));
  }

  BundlePaths bundle{};
  bundle.bundle_dir_ = bundle_dir.value();
  switch (config_.role_type_) {
  case RoleType::kAudio:
    bundle.output_mp4_ = bundle.bundle_dir_ / "audio.ts";
    break;
  case RoleType::kVideo:
    bundle.output_mp4_ = bundle.bundle_dir_ / "video.ts";
    break;
  default:
    bundle.output_mp4_ = bundle.bundle_dir_ / "output.mp4";
    break;
  }
  bundle.zip_path_ = bundle.bundle_dir_;
  bundle.zip_path_ += ".zip";

  bundle.remote_final_ = bundle.zip_path_.filename().string();
  bundle.remote_tmp_ = bundle.remote_final_ + ".tmp";
  bundle.segments_to_export_ = segments;

  return bundle;
}

VoidResult ArtifactExporter::export_mp4(RollingSegment &segmenter,
                                        const BundlePaths &bundle_paths) const {
  if (auto res = segmenter.export_last_segments(
          bundle_paths.segments_to_export_, bundle_paths.output_mp4_);
      !res) {
    return std::unexpected(
        res.error().with_context("Failed exporting segments"));
  }

  return {};
}

static VoidResult upload_zip_streaming(Nfs::IFileBackend &nfs,
                                       const std::filesystem::path &local_zip,
                                       const std::string &remote_tmp,
                                       const std::string &remote_final) {
  std::ifstream input(local_zip, std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected(make_error().with_context(
        std::format("Failed opening local zip on: '{}'", local_zip.string())));
  }

  auto file = nfs.open_write_trunc(remote_tmp);
  if (!file) {
    return std::unexpected(
        file.error().with_context("Failed opening file on nfs"));
  }

  constexpr size_t kCHUNK = 256 * 1024;
  std::vector<char> buffer(kCHUNK);

  while (input) {
    input.read(buffer.data(), buffer.size());
    std::streamsize got = input.gcount();
    if (got <= 0) {
      break;
    }

    auto write = nfs.write(*file, buffer.data(), got);
    if (!write) {
      if (auto res = nfs.close(*file); !res) {
        return std::unexpected(write.error().with_context(std::format(
            "Write failed and failed closing file : {}", res.error().what())));
      }

      return std::unexpected(
          write.error().with_context("Failed writing to nfs"));
    }
  }

  if (auto res = nfs.close(*file); !res) {
    return std::unexpected(res.error().with_context("Failed closing nfs file"));
  }

  if (auto res = nfs.rename(remote_tmp, remote_final); !res) {
    return std::unexpected(
        res.error().with_context("Failed renaming temp file"));
  }

  return {};
}
VoidResult ArtifactExporter::zip_bundle(const BundlePaths &bundle_paths) const {
  std::string cmd;
#ifdef _WIN32
  cmd = std::format(R"(7z.exe a -tzip -y -r -bso0 -bse0 "{}" "{}")",
                    bundle_paths.zip_path_.string(),
                    bundle_paths.bundle_dir_.string());
#else
  cmd = std::format(R"(7zz a -tzip -y -r -bso0 -bse0 "{}" "{}")",
                    bundle_paths.zip_path_.string(),
                    bundle_paths.bundle_dir_.string());
#endif

  if (std::system(cmd.c_str()) != 0) {
    return std::unexpected(make_error().with_context("Zip command failed"));
  }

  std::error_code errc;
  auto size = std::filesystem::file_size(bundle_paths.zip_path_, errc);
  if (errc || size < 1024) {
    return std::unexpected(make_error().with_context(
        std::format("Zip file empty: {}", errc.message())));
  }

  return {};
}
VoidResult ArtifactExporter::upload(Nfs::IFileBackend &nfs,
                                    const BundlePaths &bundle_paths) const {
  if (auto init =
          nfs.initialize(config_.nfs.server_address_, config_.nfs.export_path_);
      !init) {
    return std::unexpected(
        init.error().with_context("Failed initializing nfs client"));
  }
  if (auto upload = upload_zip_streaming(nfs, bundle_paths.zip_path_,
                                         bundle_paths.remote_tmp_,
                                         bundle_paths.remote_final_);
      !upload) {
    return std::unexpected(
        upload.error().with_context("Failed uploading zip to nfs"));
  }
  return {};
}

void ArtifactExporter::cleanup(const BundlePaths &bundle_paths) const {
  std::error_code errc;
  if (!config_.nfs.save_locally_) {
    std::filesystem::remove(bundle_paths.zip_path_, errc);
    if (errc) {
      Log::media_recorder()->warn("Failed to remove zip '{}': {}",
                                  bundle_paths.zip_path_.string(),
                                  errc.message());
    }
  }

  std::filesystem::remove_all(bundle_paths.bundle_dir_, errc);
  if (errc) {
    Log::media_recorder()->warn("Failed to remove bug folder '{}': {}",
                                bundle_paths.bundle_dir_.string(),
                                errc.message());
  }
}
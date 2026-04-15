#include "core/export/VSLogs.hpp"
#include "types.hpp"

namespace VSCapture::VSLogs {
Error::Result<std::string> read_app_version(const std::filesystem::path& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        return std::unexpected(
            Error::make_error().with_context(std::format("Failed opening version file: {}", json_path.string())));
    }

    std::ostringstream s_stream;
    s_stream << file.rdbuf();

    boost::system::error_code errc;
    json::value value = boost::json::parse(s_stream.str(), errc);

    if (errc) {
        return std::unexpected(
            Error::make_error().with_context(std::format("Failed parsing json: {}", json_path.string())));
    }

    if (!value.is_object()) {
        return std::unexpected(Error::make_error().with_context("Version json root is not an object"));
    }

    const auto& obj = value.as_object();
    const auto* iter = obj.find("defaultVersion");
    if (iter == obj.end()) {
        return std::unexpected(Error::make_error().with_context("JSON missing key: defaultVersion"));
    }

    if (!iter->value().is_string()) {
        return std::unexpected(Error::make_error().with_context("defaultVersion is not a string"));
    }

    return std::string(iter->value().as_string().c_str());
}

Error::VoidResult copy_file(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code errc;

    if (!std::filesystem::exists(src, errc) || errc) {
        return std::unexpected(
            Error::make_error().with_context(std::format("Source file does not exist: {}", src.string())));
    }

    if (!dst.parent_path().empty()) {
        std::filesystem::create_directories(dst.parent_path(), errc);
        if (errc) {
            return std::unexpected(
                Error::make_error().with_context(
                    std::format("Failed creating dir '{}': {}", dst.parent_path().string(), errc.message())));
        }
    }

    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, errc);
    if (errc) {
        return std::unexpected(
            Error::make_error().with_context(
                std::format("copy_file failed '{}' -> '{}': {}", src.string(), dst.string(), errc.message())));
    }

    return {};
}

Error::Result<std::filesystem::path> build_bundle_folder(
    const std::filesystem::path& bundles_root,
    const std::filesystem::path& src_log,
    bool save_localy,
    VsType vs_type,
    RoleType role_type) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::string prefix;
    if (!save_localy) {
        switch (role_type) {
        case RoleType::kAudio: prefix = "audio_"; break;
        case RoleType::kVideo: prefix = "video_"; break;
        default: break;
        }
    }

    const auto folder_name = prefix + std::format(
        "bug_{:02}-{:02}-{:04}_{:02}-{:02}-{:02}",
        tm.tm_mday,
        tm.tm_mon + 1,
        tm.tm_year + 1900,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec);
    std::filesystem::path bundle_dir{};

    if (save_localy) {
        bundle_dir = bundles_root / "local_recordings" / folder_name;
    }
    else {
        bundle_dir = bundles_root / folder_name;
    }

    std::error_code errc;
    std::filesystem::create_directories(bundle_dir, errc);
    if (errc) {
        return std::unexpected(
            Error::make_error().with_context(
                std::format("Failed creating bundle dir '{}': {}", bundle_dir.string(), errc.message())));
    }

    if (vs_type == VsType::kVS) {
        if (auto res = VSCapture::VSLogs::copy_file(src_log /  "AM_info.log", bundle_dir / "logs" / "AM_info.log"); !res) {
            Log::media_recorder()->warn("Failed finding log file {}, skipping...", (src_log / "AM_info.log").string());
        }
        if (auto res = VSCapture::VSLogs::copy_file(src_log / "AM_debug.log", bundle_dir / "logs" / "AM_debug.log"); !res) {
            Log::media_recorder()->warn("Failed finding log file {}, skipping...", (src_log / "AM_debug.log").string());
        }
        if (auto res = VSCapture::VSLogs::copy_file(src_log / "AM_error.log", bundle_dir / "logs" / "AM_error.log"); !res) {
            Log::media_recorder()->warn("Failed finding log file {}, skipping...", (src_log / "AM_error.log").string());
        }
    }
    else if (vs_type == VsType::kSoft) {
        const auto today = floor<std::chrono::days>(std::chrono::system_clock::now());
        const auto date = std::format("{:%Y-%m-%d}.txt", today);
        const auto log_file_path = src_log / date;

        if (auto res = VSCapture::VSLogs::copy_file(log_file_path, bundle_dir / date); !res) {
            Log::media_recorder()->warn("Failed finding log file {}, skipping...", log_file_path.string());
        }
    }

    return bundle_dir;
}
} // namespace VSCapture::VSLogs
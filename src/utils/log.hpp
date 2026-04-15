#pragma once

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string_view>
#include <system_error>
#include "spdlog/common.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "utils.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace VSCapture::Log {

inline std::filesystem::path find_newest_log(const std::filesystem::path& log_dir) {
    std::error_code errc;

    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    bool found = false;

    for (const auto& file : std::filesystem::directory_iterator(log_dir, errc)) {
        if (errc) {
            break;
        }
        if (!file.is_regular_file(errc)) {
            continue;
        }
        if (file.path().extension() != ".log") {
            continue;
        }

        const auto time = file.last_write_time(errc);
        if (errc) {
            continue;
        }

        if (!found || time < newest_time) {
            newest_time = time;
            newest = file.path();
            found = true;
        }
    }

    return newest;
}

inline void archive_last_log(const std::filesystem::path& log_dir) {
    std::error_code errc;

    std::filesystem::path newest = find_newest_log(log_dir);

    for (const auto& file : std::filesystem::directory_iterator(log_dir, errc)) {
        if (errc) {
            break;
        }
        if (!file.is_regular_file(errc)) {
            continue;
        }
        if (file.path().extension() != ".log") {
            continue;
        }
        if (file.path() == newest) {
            continue;
        }

        std::filesystem::remove(file.path(), errc);
        errc.clear();
    }

    if (!newest.empty()) {
        std::filesystem::path dst = log_dir / "last_run.log";
        std::filesystem::rename(newest, dst, errc);

        if (errc) {
            std::filesystem::copy_file(newest, dst, std::filesystem::copy_options::overwrite_existing, errc);
            std::filesystem::remove(newest, errc);
        }
    }
}

inline void init_logging(int max_log_size, int max_files) {
    const std::filesystem::path log_dir = Utils::get_exe_dir() / "logs";
    const std::filesystem::path log_file = log_dir / "VSCapture.log";

    archive_last_log(log_dir);

    try {
        std::filesystem::create_directories(log_dir);
        auto file_sink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_file.string(), max_log_size, max_files);

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        auto logger = std::make_shared<spdlog::logger>("VSCapture", spdlog::sinks_init_list{file_sink, console_sink});

        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y:%m:%d %H:%M:%S.%e] [%t] [%^%l%$] [%n] %v");
        spdlog::flush_on(spdlog::level::info);
        spdlog::flush_every(std::chrono::seconds(3));
    } catch (const spdlog::spdlog_ex& err) {
        auto logger = spdlog::stdout_color_mt("VSCapture");
        spdlog::set_default_logger(logger);
        spdlog::warn("File logging disabled: ({}). Using console only.", err.what());
    }
}

inline void set_log_level(const std::string& log_level) {
    auto level = spdlog::level::from_str(log_level);
    if (level == spdlog::level::off) {
        spdlog::info("Invalid log level: {}, setting log level to info", log_level);
        spdlog::set_level(spdlog::level::info);
    }
    else {
        spdlog::set_level(level);
    }
}

inline std::shared_ptr<spdlog::logger> make_sub_logger(const std::string& name) {
    return spdlog::default_logger()->clone(name);
}

inline std::shared_ptr<spdlog::logger> api() {
    static auto logger = make_sub_logger("api");
    return logger;
}

inline std::shared_ptr<spdlog::logger> server() {
    static auto logger = make_sub_logger("server");
    return logger;
}

inline std::shared_ptr<spdlog::logger> video_capture() {
    static auto logger = make_sub_logger("video.capture");
    return logger;
}

inline std::shared_ptr<spdlog::logger> video_encode() {
    static auto logger = make_sub_logger("video.encode");
    return logger;
}

inline std::shared_ptr<spdlog::logger> audio_capture() {
    static auto logger = make_sub_logger("audio.capture");
    return logger;
}

inline std::shared_ptr<spdlog::logger> audio_encode() {
    static auto logger = make_sub_logger("audio.encode");
    return logger;
}

inline std::shared_ptr<spdlog::logger> app() {
    static auto logger = make_sub_logger("app");
    return logger;
}

inline std::shared_ptr<spdlog::logger> media_recorder() {
    static auto logger = make_sub_logger("media recorder");
    return logger;
}

inline std::shared_ptr<spdlog::logger> sync() {
    static auto logger = make_sub_logger("sync");
    return logger;
}


inline void crash_error(const std::string_view msg) {
    Log::app()->critical(msg);
    std::quick_exit(EXIT_FAILURE);
}

} // namespace VSCapture::Log
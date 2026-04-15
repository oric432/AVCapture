#pragma once

#include <boost/lexical_cast/try_lexical_convert.hpp>
#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <cassert>
#include "boost/asio/connect.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/steady_timer.hpp"
#include "boost/system/detail/error_code.hpp"
#include "error.hpp"
#include "types.hpp"

namespace VSCapture::Utils {

inline Error::VoidResult tcp_connect_with_timeout(
    const std::string& host,
    uint16_t port, 
    std::chrono::milliseconds timeout
) {
    asio::io_context io_ctx;

    tcp::resolver resolver(io_ctx);
    tcp::socket socket(io_ctx);
    asio::steady_timer timer(io_ctx);

    bool connected = false;
    bool timeod_out = false;
    boost::system::error_code errc;

    auto endpoints = resolver.resolve(host, std::to_string(port), errc);
    if (errc) {
        return std::unexpected(
            Error::make_error().with_context("DNS Resolve failed: " + errc.message())
        );
    }

    timer.expires_after(timeout);
    timer.async_wait([&](const boost::system::error_code err) {
        if (!err && !connected) {
            timeod_out = true;
            socket.cancel();
        }
    });

    asio::async_connect(socket, endpoints, [&](const boost::system::error_code err, const tcp::endpoint&) {
        if (!err) {
            connected = true;
        }
        errc = err;
        timer.cancel();
    });

    io_ctx.run();

    if (connected) {
        return {};
    }

    if (timeod_out) {
        return std::unexpected(
            Error::make_error().with_context("TCP connect timed out")
        );
    }

    return std::unexpected(
        Error::make_error().with_context("TCP connect failed: " + errc.message())
    );
}

inline Error::Result<VsType> parse_vs_type(std::string_view type) {
    if (type == "VS") {
        return VsType::kVS;
    } 
    
    if (type == "SOFT") {
        return VsType::kSoft;
    }

    return std::unexpected(Error::make_error().with_context("Invalid vs type"));
}

inline Error::Result<RoleType> parse_role_type(std::string_view type) {
    if (type == "audio") {
        return RoleType::kAudio;
    } 
    
    if (type == "video") {
        return RoleType::kVideo;
    }

    if (type == "none") {
        return RoleType::kNone;
    }

    return std::unexpected(Error::make_error().with_context("Invalid role type"));
}

template <typename T>
std::optional<T> get_env_var(const std::string& env_var) {
    auto* var = std::getenv(env_var.c_str()); // NOLINT
    if (var) {
        T value;
        if (boost::conversion::try_lexical_convert(var, value)) {
            return value;
        }
        return {};
    }
    return {};
}

// Return the parent directory of a path
inline std::string parent_dir(std::string path) {
    if (path.empty()) {
        return {};
    }

    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return {};
    }

    if (pos == 0) {
        return "/";
    }

    return path.substr(0, pos);
}

inline std::filesystem::path get_exe_dir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buf).parent_path();
}
} // namespace VSCapture::Utils
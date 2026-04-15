#pragma once

#include <chrono>
#include <cstdint>

namespace VSCapture::Sync {
inline int64_t system_clock_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline std::chrono::steady_clock::time_point to_steady_time_point(int64_t target_unix_ns) {
    const auto now_unix = VSCapture::Sync::system_clock_now_ns();
    const auto now_steady = std::chrono::steady_clock::now();

    const auto delta_ns = target_unix_ns - now_unix;
    return now_steady + std::chrono::nanoseconds(delta_ns);
}


} // namespace VSCapture::Sync
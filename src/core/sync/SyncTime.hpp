#pragma once

#include <chrono>
#include <cstdint>

namespace VSCapture::Sync {
inline int64_t unix_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline std::chrono::steady_clock::time_point steady_deadline_from_unix_ns(int64_t target_unix_ns) {
    const auto now_unix = VSCapture::Sync::unix_now_ns();
    const auto now_steady = std::chrono::steady_clock::now();

    const auto delta_ns = target_unix_ns - now_unix;
    return now_steady + std::chrono::nanoseconds(delta_ns);
}

struct ClockMapper {
    int64_t unix0_ns_{};
    std::chrono::steady_clock::time_point steady0_;

    static ClockMapper now()
    {
        ClockMapper mapper;
        mapper.unix0_ns_ = unix_now_ns();
        mapper.steady0_= std::chrono::steady_clock::now();
        return mapper;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point to_steady_deadline(int64_t unix_ns) const {
        return steady0_ + std::chrono::nanoseconds(unix_ns - unix0_ns_);
    }
};

} // namespace VSCapture::Sync
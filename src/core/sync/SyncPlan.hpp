#pragma once 

#include "SyncTime.hpp"
#include <cstdint>
#include <thread>

namespace VSCapture::Sync {
struct SyncPlan{
    int64_t t0_master_ns_ = 0;
    int64_t offset_ns_ = 0;
    int seg_ms_ = 0;

    [[nodiscard]] int64_t t0_local_unix_nx() const { return t0_master_ns_ - offset_ns_; }

    [[nodiscard]] int64_t boundary_master_unix_ns(int k) const {
        return t0_master_ns_ + (static_cast<int64_t>(k) * static_cast<int64_t>(seg_ms_) * 1'000'000);
    }

    [[nodiscard]] int64_t boundary_local_unix_ns(int k) const {
        return boundary_master_unix_ns(k) - offset_ns_;
    }
};

inline void sleep_until_local_unix_ns(const ClockMapper& mapper, int64_t local_unix_ns) {
    std::this_thread::sleep_until(mapper.to_steady_deadline(local_unix_ns));
}
}
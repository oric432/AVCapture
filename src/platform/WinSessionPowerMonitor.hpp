#pragma once
#ifdef _WIN32
    #include <windef.h>
    #include <thread>
    #include <minwindef.h>
    #include <atomic>
    #include "utils/error.hpp"
    #include <WtsApi32.h>

    #pragma comment(lib, "Wtsapi32.lib")

namespace VSCapture::Platform {
class WinSessionPowerMonitor {
public:
    WinSessionPowerMonitor() = default;
    ~WinSessionPowerMonitor();

    Error::VoidResult start();
    void stop();

    std::atomic<bool> need_video_reinit_{false};
    std::atomic<bool> force_black_flag_{false};

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wpar, LPARAM lpar);

    Error::VoidResult create_window();

    std::jthread th_;

    HWND hwnd_{nullptr};
    DWORD thread_id_{0};
};
} // namespace VSCapture::Platform

#endif
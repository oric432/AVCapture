#pragma once
#ifdef _WIN32
#include "utils/error.hpp"
#include <WtsApi32.h>
#include <atomic>
#include <minwindef.h>
#include <thread>
#include <windef.h>

#pragma comment(lib, "Wtsapi32.lib")

namespace AVCapture::Platform {
class WinSessionPowerMonitor {
public:
  WinSessionPowerMonitor() = default;
  ~WinSessionPowerMonitor();

  Error::VoidResult start();
  void stop();

  std::atomic<bool> need_video_reinit_{false};
  std::atomic<bool> force_black_flag_{false};

private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wpar,
                                  LPARAM lpar);

  Error::VoidResult create_window();

  std::jthread th_;

  HWND hwnd_{nullptr};
  DWORD thread_id_{0};
};
} // namespace AVCapture::Platform

#endif
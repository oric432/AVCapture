#ifdef _WIN32
#include "WinSessionPowerMonitor.hpp"
#include <errhandlingapi.h>
#include <libloaderapi.h>
#include <processthreadsapi.h>
#include <winerror.h>
#include <winuser.h>

using namespace AVCapture::Platform;
using namespace AVCapture::Error;
using namespace AVCapture;

WinSessionPowerMonitor::~WinSessionPowerMonitor() { stop(); }

VoidResult WinSessionPowerMonitor::start() {
  if (th_.joinable()) {
    return {};
  }

  th_ = std::jthread([this](std::stop_token stoken) {
    thread_id_ = GetCurrentThreadId();

    // Win32 threads don't always have a message queue
    // PeekMessageW ensures the queue exists so PostThreadMessageW will work
    // reliably
    MSG msg{};
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);

    // Create a hidden window that will receive the messages
    if (auto res = create_window(); !res) {
      return;
    }

    // Standard Win32 message loop
    while (!stoken.stop_requested()) {
      // blocks until a message is received
      BOOL res = GetMessageW(&msg, nullptr, 0, 0);
      if (res <= 0) {
        break;
      }

      TranslateMessage(&msg);
      // Calls our WndProc with the messaqge
      DispatchMessageW(&msg);
    }

    // Cleanup
    if (hwnd_) {
      WTSUnRegisterSessionNotification(hwnd_);
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
  });

  return {};
}
void WinSessionPowerMonitor::stop() {
  // Wake up message loop so GetMessageW returns
  if (thread_id_ != 0) {
    PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
  }

  if (!th_.joinable()) {
    return;
  }

  // Stop token
  th_.request_stop();
  hwnd_ = nullptr;
  thread_id_ = 0;
}

LRESULT CALLBACK WinSessionPowerMonitor::WndProc(HWND hwnd, UINT msg,
                                                 WPARAM wpar, LPARAM lpar) {
  WinSessionPowerMonitor *self = nullptr;

  if (msg == WM_NCCREATE) {
    // WM_NCCREATE is the first message during window creation.
    // lpar contains the CREATESTRUCTW*, where lpCreateParams is what we passed
    // to CreateWindowExW
    auto *crestr = reinterpret_cast<CREATESTRUCTW *>(lpar);
    self = reinterpret_cast<WinSessionPowerMonitor *>(crestr->lpCreateParams);

    // Store 'self' pointer in the window so future messages can retrieve it
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

    // Let default processing continue
    return DefWindowProcW(hwnd, msg, wpar, lpar);
  }

  // Retrieve the stored pointer for normal messages
  self = reinterpret_cast<WinSessionPowerMonitor *>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) {
    return DefWindowProcW(hwnd, msg, wpar, lpar);
  }

  switch (msg) {
  case WM_WTSSESSION_CHANGE: {
    // Session changes: lock/unlock
    if (wpar == WTS_SESSION_LOCK) {
      // Screen locked
      self->force_black_flag_.store(true, std::memory_order::release);
      self->need_video_reinit_.store(false, std::memory_order::release);
    }
    if (wpar == WTS_SESSION_UNLOCK) {
      // Screen unlocked
      self->force_black_flag_.store(false, std::memory_order::release);
      self->need_video_reinit_.store(true, std::memory_order::release);
    }
    return 0;
  }

  case WM_POWERBROADCAST: {
    // Power events: sleep/resume
    if (wpar == PBT_APMSUSPEND) {
      // System is entering suspended hence desktop duplication will break
      self->force_black_flag_.store(true, std::memory_order::release);
      return TRUE;
    }

    if (wpar == PBT_APMRESUMESUSPEND || wpar == PBT_APMRESUMEAUTOMATIC) {
      // System resumed hence desktop duplication will require an
      // re-initialization
      self->need_video_reinit_.store(true, std::memory_order::release);
      self->force_black_flag_.store(false, std::memory_order::release);
    }

    return TRUE;
  }

  // Asked to close the window; destroy it
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;

  // Post WM_QUIT to end GetMessageW loop
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;

  default:
    return DefWindowProcW(hwnd, msg, wpar, lpar);
  }
}

VoidResult WinSessionPowerMonitor::create_window() {
  constexpr auto kClassName = L"VSCapture_SessionPowerMonitor";

  // WNDCLASSEXW registers a window class that binds a WndProc callback.
  // We must register a class before creating windows of that class.
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &WinSessionPowerMonitor::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClassName;

  if (RegisterClassExW(&wc) == 0U) {
    const DWORD err = GetLastError();

    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      return std::unexpected(make_error().with_context(
          std::format("RegisterClassExW failed: {}", (unsigned)err)));
    }
  }

  // Creates a window. We use HWND_MEESSAGE to create a
  // "message-only" window which is not visible not on taskbar and exists purely
  // to reveive messages
  hwnd_ = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                          nullptr, wc.hInstance, this);
  if (hwnd_ == nullptr) {
    const DWORD err = GetLastError();
    return std::unexpected(make_error().with_context(
        std::format("CreateWindowExW failed: {}", (unsigned)err)));
  }

  // Register notifications for this session
  if (WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) == 0) {
    const DWORD err = GetLastError();
    (void)err;
  }

  return {};
}

#endif
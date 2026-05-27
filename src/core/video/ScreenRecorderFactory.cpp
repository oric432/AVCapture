#include "IScreenRecorder.hpp"

#ifdef _WIN32
#include "platform/ScreenRecorderWindows.hpp"
#elif defined(__linux__)
#include "platform/ScreenRecorderLinux.hpp"
#endif

namespace AVCapture::Platform {

std::unique_ptr<IScreenRecorder> create_screen_recorder() {
#ifdef _WIN32
  return std::make_unique<ScreenRecorderWindows>();
#elif defined(__linux__)
  return std::make_unique<ScreenRecorderLinux>();
#else
#error "Unsupported platform"
  return nullptr;
#endif
}
} // namespace AVCapture::Platform
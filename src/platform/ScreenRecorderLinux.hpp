#pragma once

#ifdef __linux__

#include "core/video/ScreenRecorderBase.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xrandr.h>
#include <sys/ipc.h>
#include <sys/shm.h>

namespace VSCapture::Platform {

/**
 * @brief Linux implementation using X11 for screen capture
 */
class ScreenRecorderLinux : public Platform::ScreenRecorderBase {
public:
  ScreenRecorderLinux();
  ~ScreenRecorderLinux() override;

protected:
  Error::VoidResult init_platform() override;
  void capture_frame() override;
  void cleanup_platform() override;
  Core::RotationType get_rotation_type(int rotation) override;

private:
  Error::Result<Core::RotationType> detect_screen_rotation();

  // X11-specific members
  Display *display_ = nullptr;
  Window root_window_ = 0;
  XImage *ximage_ = nullptr;
  XShmSegmentInfo shminfo_{};
  bool use_shm_ = false;
  int32_t screen_num_ = 0;
};

} // namespace VSCapture::Platform

#endif // __linux__
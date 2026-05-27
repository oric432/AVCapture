#pragma once

#ifdef _WIN32

#include <atomic>
#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <wrl/client.h>

#include "WinSessionPowerMonitor.hpp"
#include "core/video/ScreenRecorderBase.hpp"

using Microsoft::WRL::ComPtr;

namespace AVCapture::Platform {

/**
 * @brief Windows implementation using DirectX Desktop Duplication API
 */
class ScreenRecorderWindows : public Platform::ScreenRecorderBase {
public:
  ScreenRecorderWindows();
  ~ScreenRecorderWindows() override;

protected:
  Error::VoidResult init_platform() override;
  void capture_frame() override;
  void cleanup_platform() override;
  Core::RotationType get_rotation_type(int rotation) override;

private:
  Error::VoidResult create_device();
  Error::VoidResult create_desk_duplication();
  Error::VoidResult recreate_desk_duplication();
  Error::VoidResult create_staging_texture();
  Error::VoidResult init_encoder();

  Error::VoidResult reinit_duplication();
  Error::VoidResult reinit();

  void ensure_black_frame();
  void push_black_frame(int64_t pts);

  // DirectX-specific members
  ComPtr<ID3D11Device> d3d_device_;
  ComPtr<ID3D11DeviceContext> d3d_context_;
  ComPtr<IDXGIOutputDuplication> desk_dupl_;
  ComPtr<ID3D11Texture2D> staging_texture_;

  std::unique_ptr<AVCapture::Platform::WinSessionPowerMonitor> monitor_;
  std::atomic<bool> need_reinit_{false};
  std::atomic<bool> force_black_{false};
  std::vector<uint8_t> black_frame_;
};

} // namespace AVCapture::Platform

#endif // _WIN32
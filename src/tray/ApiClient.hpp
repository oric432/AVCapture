#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace AVCapture::Tray {

struct ApiStatus {
  bool recording{false};
};

// Small synchronous HTTP client for the recorder's local REST API.
// Uses Boost.Beast directly (already vendored for the recorder) instead of
// QtNetwork, since the tray only fetches QtBase+Widgets.
class ApiClient {
public:
  ApiClient(std::string host, unsigned short port);

  [[nodiscard]] bool is_healthy(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) const;

  [[nodiscard]] std::optional<ApiStatus> get_status(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) const;

  // Fire-and-forget graceful shutdown request. The server saves the
  // in-progress recording (if any), replies, then exits after a short delay.
  void request_shutdown(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) const;

  // Saves the current buffer to disk. Recording continues uninterrupted.
  [[nodiscard]] bool save_recording(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) const;

private:
  [[nodiscard]] std::optional<std::string>
  perform_request(const char *method, const char *target,
                  std::chrono::milliseconds timeout) const;

  std::string host_;
  unsigned short port_;
};

} // namespace AVCapture::Tray

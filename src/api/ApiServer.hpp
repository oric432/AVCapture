#pragma once

#include "core/MediaRecorder.hpp"
#include "types.hpp"
#include "utils/error.hpp"

#include <functional>
#include <string>

namespace AVCapture::Api {

class ApiServer {
public:
  static Error::Result<ApiServer> create(asio::io_context &ioc,
                                         const std::string &address,
                                         unsigned short port,
                                         Core::MediaRecorder *recorder,
                                         std::function<void()> on_shutdown = {});

  ApiServer(asio::io_context &ioc, Core::MediaRecorder *recorder,
            std::function<void()> on_shutdown = {});
  ApiServer(ApiServer &&) noexcept = default;
  ApiServer &operator=(ApiServer &&) noexcept = default;

  ApiServer(const ApiServer &) = delete;
  ApiServer &operator=(const ApiServer &) = delete;

  void run();

private:
  void do_accept();

  tcp::acceptor acceptor_;
  Core::MediaRecorder *recorder_;
  std::function<void()> on_shutdown_;
};
} // namespace AVCapture::Api

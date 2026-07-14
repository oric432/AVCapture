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
                                         std::function<void()> on_shutdown = {},
                                         std::string api_key = {});

  ApiServer(asio::io_context &ioc, Core::MediaRecorder *recorder,
            std::function<void()> on_shutdown = {},
            std::string api_key = {});
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
  std::string api_key_;
};
} // namespace AVCapture::Api

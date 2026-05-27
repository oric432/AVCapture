#pragma once

#include "core/MediaRecorder.hpp"
#include "types.hpp"
#include "utils/error.hpp"

#include <string>

namespace VSCapture::Api {

class ApiServer {
public:
  static Error::Result<ApiServer> create(asio::io_context &ioc,
                                         const std::string &address,
                                         unsigned short port,
                                         Core::MediaRecorder *recorder);

  ApiServer(asio::io_context &ioc, Core::MediaRecorder *recorder);
  ApiServer(ApiServer &&) noexcept = default;
  ApiServer &operator=(ApiServer &&) noexcept = default;

  ApiServer(const ApiServer &) = delete;
  ApiServer &operator=(const ApiServer &) = delete;

  void run();

private:
  void do_accept();

  tcp::acceptor acceptor_;
  Core::MediaRecorder *recorder_;
};
} // namespace VSCapture::Api

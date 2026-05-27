#pragma once

#include "core/MediaRecorder.hpp"
#include "types.hpp"


namespace VSCapture::Api {

class ApiServer {
public:
  ApiServer(asio::io_context &ioc, const std::string &address,
            unsigned short port, Core::MediaRecorder *recorder);
  void run();

private:
  void do_accept();

  tcp::acceptor acceptor_;
  Core::MediaRecorder *recorder_;
};
} // namespace VSCapture::Api
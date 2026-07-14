#pragma once
#include "types.hpp"

#include "Router.hpp"
#include "core/MediaRecorder.hpp"
#include <functional>
#include <string>

namespace AVCapture::Api {

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
  HttpSession(tcp::socket socket, Core::MediaRecorder *recorder,
              std::function<void()> on_shutdown = {},
              std::string api_key = {});
  void run();

private:
  void do_read();
  void handle_request();
  void do_write(http::response<http::string_body> res);

  tcp::socket socket_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> req_;
  std::shared_ptr<Router> router_;
};
} // namespace AVCapture::Api

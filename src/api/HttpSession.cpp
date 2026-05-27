
#include "HttpSession.hpp"
using namespace AVCapture::Api;

HttpSession::HttpSession(tcp::socket socket, Core::MediaRecorder *recorder)
    : socket_(std::move(socket)), router_(std::make_shared<Router>(recorder)) {}

void HttpSession::run() { do_read(); }

void HttpSession::do_read() {
  auto self = shared_from_this();
  http::async_read(socket_, buffer_, req_,
                   [self](beast::error_code errc, std::size_t) {
                     if (!errc) {
                       self->handle_request();
                     }
                   });
}

void HttpSession::handle_request() {
  auto res = router_->handle(req_);
  do_write(std::move(res));
}

void HttpSession::do_write(http::response<http::string_body> res) {
  auto self = shared_from_this();
  auto sb_res =
      std::make_shared<http::response<http::string_body>>(std::move(res));

  http::async_write(
      socket_, *sb_res, [self, sb_res](beast::error_code errc, std::size_t) {
        errc = self->socket_.shutdown(tcp::socket::shutdown_send, errc);
      });
}

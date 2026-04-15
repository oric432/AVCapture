#pragma once
#include "types.hpp"

#include "AppContext.hpp"
#include "Router.hpp"

namespace VSCapture::Api {

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, AppContext app_context);
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
} // namespace VSCapture::Api

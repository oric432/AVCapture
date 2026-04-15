#pragma once

#include "types.hpp"
#include "AppContext.hpp"

namespace VSCapture::Api {

class ApiServer {
public:
    ApiServer(asio::io_context& ioc, const std::string& address, unsigned short port, AppContext app_context);
    void run();

private:
    void do_accept();

    tcp::acceptor acceptor_;
    AppContext app_context_;
};
} // namespace VSCapture::Api
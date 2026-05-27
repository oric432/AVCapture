#include "ApiServer.hpp"
#include "HttpSession.hpp"

using namespace VSCapture::Api;

ApiServer::ApiServer(asio::io_context &ioc, const std::string &address,
                     unsigned short port, Core::MediaRecorder *recorder)
    : acceptor_(ioc), recorder_(recorder) {
  beast::error_code err;
  auto boost_addr = boost::asio::ip::make_address(address, err);
  if (err) {
    Log::api()->info("Failed making address: {}", err.message());
    std::quick_exit(EXIT_FAILURE);
    return;
  }

  tcp::endpoint endpoint(boost_addr, port);
  if (err) {
    Log::api()->info("Endpoint creation error: {}", err.message());
    std::quick_exit(EXIT_FAILURE);
    return;
  }

  // Open the acceptor
  err = acceptor_.open(endpoint.protocol(), err);
  if (err) {
    Log::api()->info("Acceptor open error: {}", err.message());
    std::quick_exit(EXIT_FAILURE);
    return;
  }

  // Allow address reuse
  acceptor_.set_option(asio::socket_base::reuse_address(true), err);
  if (err) {
    Log::api()->critical("Set reuse address error: {}", err.message());
    std::quick_exit(EXIT_FAILURE);
    return;
  }

  // Bind to the server address
  acceptor_.bind(endpoint, err);
  if (err) {
    Log::api()->critical("Bind error error: {} (binding {}:{})", err.message(),
                         address, port);
    std::quick_exit(EXIT_FAILURE);
    return;
  }

  // Start listening for connections
  acceptor_.listen(asio::socket_base::max_listen_connections, err);
  if (err) {
    Log::api()->critical("Listen error : {}", err.message());
    std::quick_exit(EXIT_FAILURE);
    return;
  }

  Log::api()->info("Started Api server at: {}:{}", address, port);
}
void ApiServer::run() { do_accept(); }

void ApiServer::do_accept() {
  acceptor_.async_accept([this](beast::error_code errc, tcp::socket socket) {
    if (!errc) {
      std::make_shared<HttpSession>(std::move(socket), recorder_)->run();
    }
    do_accept();
  });
}
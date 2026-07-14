#include "ApiServer.hpp"
#include "HttpSession.hpp"

using namespace AVCapture::Api;
using namespace AVCapture::Error;

ApiServer::ApiServer(asio::io_context &ioc, Core::MediaRecorder *recorder,
                     std::function<void()> on_shutdown, std::string api_key)
    : acceptor_(ioc), recorder_(recorder),
      on_shutdown_(std::move(on_shutdown)), api_key_(std::move(api_key)) {}

Result<ApiServer> ApiServer::create(asio::io_context &ioc,
                                    const std::string &address,
                                    unsigned short port,
                                    Core::MediaRecorder *recorder,
                                    std::function<void()> on_shutdown,
                                    std::string api_key) {
  Result<ApiServer> server{std::in_place, ioc, recorder,
                           std::move(on_shutdown), std::move(api_key)};

  beast::error_code err;
  auto boost_addr = boost::asio::ip::make_address(address, err);
  if (err) {
    return std::unexpected(make_error().with_context(
        std::format("Failed making address '{}': {}", address, err.message())));
  }

  tcp::endpoint endpoint(boost_addr, port);

  // Open the acceptor
  err = server->acceptor_.open(endpoint.protocol(), err);
  if (err) {
    return std::unexpected(make_error().with_context(
        std::format("Acceptor open error: {}", err.message())));
  }

  // Allow address reuse
  err =
      server->acceptor_.set_option(asio::socket_base::reuse_address(true), err);
  if (err) {
    return std::unexpected(make_error().with_context(
        std::format("Set reuse address error: {}", err.message())));
  }

  // Bind to the server address
  err = server->acceptor_.bind(endpoint, err);
  if (err) {
    return std::unexpected(make_error().with_context(std::format(
        "Bind error: {} (binding {}:{})", err.message(), address, port)));
  }

  // Start listening for connections
  err =
      server->acceptor_.listen(asio::socket_base::max_listen_connections, err);
  if (err) {
    return std::unexpected(make_error().with_context(
        std::format("Listen error: {}", err.message())));
  }

  Log::api()->info("Started Api server at: {}:{}", address, port);
  return server;
}

void ApiServer::run() { do_accept(); }

void ApiServer::do_accept() {
  acceptor_.async_accept([this](beast::error_code errc, tcp::socket socket) {
    if (!errc) {
      std::make_shared<HttpSession>(std::move(socket), recorder_,
                                    on_shutdown_, api_key_)
          ->run();
    }
    do_accept();
  });
}

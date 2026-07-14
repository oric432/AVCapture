#include "ApiClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace json = boost::json;
using tcp = asio::ip::tcp;

namespace AVCapture::Tray {

ApiClient::ApiClient(std::string host, unsigned short port)
    : host_(std::move(host)), port_(port) {}

std::optional<std::string>
ApiClient::perform_request(const char *method, const char *target,
                           std::chrono::milliseconds timeout) const {
  try {
    asio::io_context ioc;
    beast::tcp_stream stream(ioc);
    stream.expires_after(timeout);

    tcp::resolver resolver(ioc);
    const auto endpoints = resolver.resolve(host_, std::to_string(port_));
    stream.connect(endpoints);

    http::request<http::empty_body> req{
        method == std::string_view{"GET"} ? http::verb::get : http::verb::post,
        target, 11};
    req.set(http::field::host, host_);
    req.set(http::field::content_length, "0");

    stream.expires_after(timeout);
    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(timeout);
    http::read(stream, buffer, res);

    beast::get_lowest_layer(stream).close();

    return res.body();
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

bool ApiClient::is_healthy(std::chrono::milliseconds timeout) const {
  return perform_request("GET", "/health", timeout).has_value();
}

std::optional<ApiStatus>
ApiClient::get_status(std::chrono::milliseconds timeout) const {
  auto body = perform_request("GET", "/status", timeout);
  if (!body) {
    return std::nullopt;
  }

  boost::system::error_code ec;
  auto parsed = json::parse(*body, ec);
  if (ec || !parsed.is_object()) {
    return std::nullopt;
  }

  ApiStatus status;
  if (auto *recording = parsed.as_object().if_contains("recording");
      recording != nullptr && recording->is_bool()) {
    status.recording = recording->as_bool();
  }
  return status;
}

void ApiClient::request_shutdown(std::chrono::milliseconds timeout) const {
  [[maybe_unused]] auto body = perform_request("POST", "/shutdown", timeout);
}

} // namespace AVCapture::Tray

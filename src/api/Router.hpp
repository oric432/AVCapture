#pragma once
#include "types.hpp" // IWYU pragma: export

#include "boost/beast/http/string_body_fwd.hpp"
#include "core/MediaRecorder.hpp"
#include <functional>
#include <string>
#include <string_view>

namespace AVCapture::Api {

namespace Routes {
constexpr std::string_view kSTOP = "/stop";
constexpr std::string_view kSTATUS = "/status";
constexpr std::string_view kHEALTH = "/health";
constexpr std::string_view kSHUTDOWN = "/shutdown";
} // namespace Routes

// Header clients must send the configured API key in, when one is set.
constexpr std::string_view kApiKeyHeader = "X-API-Key";

class Router {
public:
  explicit Router(Core::MediaRecorder *recorder,
                  std::function<void()> on_shutdown = {},
                  std::string api_key = {});
  http::response<http::string_body>
  handle(const http::request<http::string_body> &req);

private:
  static http::response<http::string_body>
  json_response(const json::object &obj, unsigned version);
  static http::response<http::string_body>
  error_response(std::string_view message, http::status status,
                 unsigned version);
  static http::response<http::string_body>
  handle_health(const http::request<http::string_body> &req);
  static http::response<http::string_body>
  not_found_response(const http::request<http::string_body> &req);

  [[nodiscard]] bool
  is_authorized(const http::request<http::string_body> &req) const;

  http::response<http::string_body>
  handle_stop(const http::request<http::string_body> &req);

  [[nodiscard]] http::response<http::string_body>
  handle_status(const http::request<http::string_body> &req) const;

  http::response<http::string_body>
  handle_shutdown(const http::request<http::string_body> &req);

  Core::MediaRecorder *recorder_;
  std::function<void()> on_shutdown_;
  std::string api_key_;
};
} // namespace AVCapture::Api
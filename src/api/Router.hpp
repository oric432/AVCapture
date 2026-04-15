#pragma once
#include "types.hpp" // IWYU pragma: export

#include "AppContext.hpp"
#include "boost/beast/http/string_body_fwd.hpp"
#include <string_view>

namespace VSCapture::Api {

namespace Routes {
constexpr std::string_view kSTOP = "/stop";
constexpr std::string_view kSTATUS = "/status";
constexpr std::string_view kHEALTH = "/health";
} // namespace Routes

class Router {
public:
    explicit Router(AppContext app_context);
    http::response<http::string_body> handle(const http::request<http::string_body>& req);

private:
    static http::response<http::string_body> json_response(const json::object& obj, unsigned version);
    static http::response<http::string_body>
    error_response(std::string_view message, http::status status, unsigned version);
    static http::response<http::string_body> handle_health(const http::request<http::string_body>& req);
    static http::response<http::string_body> not_found_response(const http::request<http::string_body>& req);

    http::response<http::string_body> handle_stop(const http::request<http::string_body>& req);

    [[nodiscard]] http::response<http::string_body> handle_status(const http::request<http::string_body>& req) const;

    AppContext app_context_;
};
} // namespace VSCapture::Api
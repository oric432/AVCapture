
#include "Router.hpp"

using namespace AVCapture::Api;

namespace {
constexpr std::string_view kSuccess = "success";
constexpr std::string_view kError = "error";
constexpr std::string_view kRecordingName = "recordingName";
constexpr std::string_view kRecording = "recording";
constexpr std::string_view kStatus = "status";
} // namespace

Router::Router(Core::MediaRecorder *recorder,
              std::function<void()> on_shutdown)
    : recorder_(recorder), on_shutdown_(std::move(on_shutdown)) {}

http::response<http::string_body>
Router::handle(const http::request<http::string_body> &req) {
  if (req.method() == http::verb::post && req.target() == Routes::kSTOP) {
    return handle_stop(req);
  }

  if (req.method() == http::verb::get && req.target() == Routes::kSTATUS) {
    return handle_status(req);
  }

  if (req.method() == http::verb::get && req.target() == Routes::kHEALTH) {
    return handle_health(req);
  }

  if (req.method() == http::verb::post && req.target() == Routes::kSHUTDOWN) {
    return handle_shutdown(req);
  }

  return not_found_response(req);
}

http::response<http::string_body>
Router::handle_stop(const http::request<http::string_body> &req) {
  if (!recorder_->is_recording()) {
    return error_response("Not recording", http::status::bad_request,
                          req.version());
  }
  json::object response_json;
  if (auto res = recorder_->save_recording_async(); !res) {
    Log::api()->error("Failed starting background save: {}",
                      res.error().what());
    response_json[kSuccess] = false;
    response_json[kError] = res.error().what();
  } else {
    response_json[kSuccess] = true;
    response_json[kRecordingName] = res.value();
  }
  return json_response(response_json, req.version());
}
http::response<http::string_body>
Router::handle_status(const http::request<http::string_body> &req) const {
  json::object response_json;
  response_json[kRecording] = recorder_->is_recording();
  return json_response(response_json, req.version());
}
http::response<http::string_body>
Router::handle_health(const http::request<http::string_body> &req) {
  json::object response_json;
  response_json[kStatus] = "healthy";
  return json_response(response_json, req.version());
}
http::response<http::string_body>
Router::handle_shutdown(const http::request<http::string_body> &req) {
  json::object response_json;
  response_json[kSuccess] = true;
  if (on_shutdown_) {
    on_shutdown_();
  }
  return json_response(response_json, req.version());
}
http::response<http::string_body> Router::json_response(const json::object &obj,
                                                        unsigned version) {
  http::response<http::string_body> res{http::status::ok, version};
  res.set(http::field::content_type, "application/json");
  res.body() = json::serialize(obj);
  res.prepare_payload();
  return res;
}
http::response<http::string_body>
Router::error_response(std::string_view message, http::status status,
                       unsigned version) {
  http::response<http::string_body> res{status, version};
  res.set(http::field::content_type, "application/json");

  json::object error_json;
  error_json[kError] = message;
  res.body() = json::serialize(error_json);
  res.prepare_payload();
  return res;
}
http::response<http::string_body>
Router::not_found_response(const http::request<http::string_body> &req) {
  return error_response("Route not found", http::status::not_found,
                        req.version());
}

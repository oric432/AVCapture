
#include "Router.hpp"
#include "core/sync/SyncTime.hpp"

using namespace VSCapture::Api;

Router::Router(AppContext app_context)
    : app_context_(app_context) {}

http::response<http::string_body> Router::handle(const http::request<http::string_body>& req) {
    if (req.method() == http::verb::post && req.target() == Routes::kSTOP) {
        return handle_stop(req);
    }

    if (req.method() == http::verb::get && req.target() == Routes::kSTATUS) {
        return handle_status(req);
    }

    if (req.method() == http::verb::get && req.target() == Routes::kHEALTH) {
        return handle_health(req);
    }

    return not_found_response(req);
}

http::response<http::string_body> Router::handle_stop(const http::request<http::string_body>& req) {
    json::object response_json;
    if (app_context_.recorder != nullptr) {
        if (!app_context_.recorder->is_recording()) {
            return error_response("Not recording", http::status::bad_request, req.version());
        }
        if (auto res = app_context_.recorder->save_and_upload_async(); !res) {
            Log::api()->error("Failed saving buffer to file: {}", res.error().what());
            response_json["status"] = "failed";
            response_json["error"] = std::format("Failed saving buffer to file: {}", res.error().what());
        }
        else {
            response_json["status"] = "stopped";
        }
    }
    else if (app_context_.server != nullptr) {
        const int64_t master_ns = Sync::system_clock_now_ns();
        app_context_.server->send_export_at(master_ns, "output.mp4");
        response_json["status"] = "scheduled stop";
    }

    return json_response(response_json, req.version());
}
http::response<http::string_body> Router::handle_status(const http::request<http::string_body>& req) const {
    json::object response_json;
    response_json["recording"] = app_context_.recorder->is_recording();

    return json_response(response_json, req.version());
}
http::response<http::string_body> Router::handle_health(const http::request<http::string_body>& req) {
    json::object response_json;
    response_json["status"] = "healthy";
    return json_response(response_json, req.version());
}
http::response<http::string_body> Router::json_response(const json::object& obj, unsigned version) {
    http::response<http::string_body> res{http::status::ok, version};
    res.set(http::field::content_type, "application/json");
    res.body() = json::serialize(obj);
    res.prepare_payload();
    return res;
}
http::response<http::string_body>
Router::error_response(std::string_view message, http::status status, unsigned version) {
    http::response<http::string_body> res{status, version};
    res.set(http::field::content_type, "application/json");

    json::object error_json;
    error_json["error"] = message;
    res.body() = json::serialize(error_json);
    res.prepare_payload();
    return res;
}
http::response<http::string_body> Router::not_found_response(const http::request<http::string_body>& req) {
    return error_response("Route not found", http::status::not_found, req.version());
}

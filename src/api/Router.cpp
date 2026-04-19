
#include "Router.hpp"
#include "core/sync/SyncTime.hpp"

using namespace VSCapture::Api;

namespace {
constexpr std::string_view kSuccess            = "success";
constexpr std::string_view kError              = "error";
constexpr std::string_view kVsType             = "vsType";
constexpr std::string_view kRecordingName      = "recordingName";
constexpr std::string_view kAudioRecordingName = "audioRecordingName";
constexpr std::string_view kRecording          = "recording";
constexpr std::string_view kStatus             = "status";
} // namespace

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
    if (auto* rec = std::get_if<Core::MediaRecorder*>(&app_context_)) {
        if (!(*rec)->is_recording()) {
            return error_response("Not recording", http::status::bad_request, req.version());
        }
        json::object response_json;
        if (auto res = (*rec)->save_and_upload_async(); !res) {
            Log::api()->error("Failed saving buffer to file: {}", res.error().what());
            response_json[kSuccess] = false;
            response_json[kError]   = res.error().what();
        } else {
            response_json[kSuccess]       = true;
            response_json[kRecordingName] = res.value();
            response_json[kVsType]        = (*rec)->role_type() == RoleType::kVideo ? "SPLIT" : "VS";
        }
        return json_response(response_json, req.version());
    }

    if (auto* srv = std::get_if<Sync::SyncMasterServer*>(&app_context_)) {
        const int64_t master_ns = Sync::system_clock_now_ns();
        auto names = (*srv)->send_save_at(master_ns);
        json::object response_json;
        response_json[kSuccess]            = true;
        response_json[kVsType]             = "SPLIT";
        response_json[kRecordingName]      = names.video_name;
        response_json[kAudioRecordingName] = names.audio_name;
        return json_response(response_json, req.version());
    }

    return error_response("No context", http::status::internal_server_error, req.version());
}
http::response<http::string_body> Router::handle_status(const http::request<http::string_body>& req) const {
    json::object response_json;
    if (auto* rec = std::get_if<Core::MediaRecorder*>(&app_context_)) {
        response_json[kRecording] = (*rec)->is_recording();
    } else {
        response_json[kRecording] = false;
    }
    return json_response(response_json, req.version());
}
http::response<http::string_body> Router::handle_health(const http::request<http::string_body>& req) {
    json::object response_json;
    response_json[kStatus] = "healthy";
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
    error_json[kError] = message;
    res.body() = json::serialize(error_json);
    res.prepare_payload();
    return res;
}
http::response<http::string_body> Router::not_found_response(const http::request<http::string_body>& req) {
    return error_response("Route not found", http::status::not_found, req.version());
}

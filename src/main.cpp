#include "core/MediaRecorder.hpp"
#include "Settings.hpp"
#include "api/ApiServer.hpp"
#include "core/video/IScreenRecorder.hpp"
#include "core/sync/SyncMasterServer.hpp"
#include "core/sync/SyncTime.hpp"
#include "core/sync/SyncWorkerClient.hpp"
#include "types.hpp"
#include "utils/log.hpp"
#include "utils/utils.hpp"

using namespace VSCapture;

//  libnfs requires wsa initialization
#ifdef WIN32
    #include <win32/win32_compat.h>
    #include <consoleapi3.h>
    #include <libloaderapi.h>
    #include <minwindef.h>
    #include <winuser.h>
    #pragma comment(lib, "ws2_32.lib")
// an RAII wrapper for the winsock initialization
struct WinsockRAII {
    WSADATA wsa_{};
    WinsockRAII() {
        const int res = WSAStartup(MAKEWORD(2, 2), &wsa_);
        if (res != 0) {
            spdlog::error("WSAStartup failed {}", res);
            std::quick_exit(1);
        }
    }

    WinsockRAII(const WinsockRAII&) = default;
    WinsockRAII(WinsockRAII&&) = default;
    WinsockRAII& operator=(const WinsockRAII&) = default;
    WinsockRAII& operator=(WinsockRAII&&) = default;
    ~WinsockRAII() { WSACleanup(); }
};

#endif

namespace {

bool has_arg(int argc, char** argv, std::string_view key) {
    for (int i = 0; i < argc; ++i) {
        if (std::string_view(argv[i]) == key) {
            return true;
        }
    }

    return false;
}

void platform_init(int argc, char** argv) {
#ifdef WIN32
    static WinsockRAII winsock; // static so it lives until process exit

    // Needed for TaskScheduler
    std::filesystem::current_path(Utils::get_exe_dir());

    // TaskScheduler background mode (hide console)
    if (has_arg(argc, argv, "--background")) {
        HWND console = GetConsoleWindow();
        if (console != nullptr) {
            ShowWindow(console, SW_HIDE);
        }
    }
#else
    (void)argc;
    (void)argv;
#endif
}

void init_logging_from_settings(const Settings& settings) {
    const auto max_log_size = settings.get<int>(Settings::Path::kMAX_LOG_SIZE_BYTES);
    const auto max_log_files = settings.get<int>(Settings::Path::kMAX_FILES);

    Log::init_logging(max_log_size, max_log_files);
    Log::set_log_level(settings.get<std::string>(Settings::Path::kLOG_LEVEL));
}

Platform::RecordingConfig make_recording_config(const Settings& settings) {
    Platform::RecordingConfig cfg;
    cfg.fps_ = settings.get<int>(Settings::Path::kFPS);
    cfg.bitrate_ = settings.get<int>(Settings::Path::kBITRATE);
    cfg.buffer_duration_ = settings.get<int>(Settings::Path::kBUFFER_DURATION);
    cfg.segment_seconds_ = settings.get<int>(Settings::Path::kSEGMENT_SECONDS);

    cfg.output_device_name_ = settings.get<std::string>(Settings::Path::kOUTPUT_DEVICE_NAME);
    cfg.input_device_name_ = settings.get<std::string>(Settings::Path::kINPUT_DEVICE_NAME);

    cfg.save_locally_ = settings.get<bool>(Settings::Path::kSAVE_LOCALLY);
    cfg.server_address_ = settings.get<std::string>(Settings::Path::kNFS_SERVER_ADDRESS);
    cfg.export_path_ = settings.get<std::string>(Settings::Path::kNFS_EXPORT_PATH);
    cfg.vs_app_path_ = settings.get<std::string>(Settings::Path::kVS_APP_PATH);

    const auto vs_type_str = settings.get<std::string>(Settings::Path::kVS_TYPE);
    if (auto vs_type = Utils::parse_vs_type(vs_type_str)) {
        cfg.vs_type_ = *vs_type;
    }
    else {
        Log::app()->warn("Unrecognized vs type, Got: {}, Expected: VS/SOFT, defaulting to VS...", vs_type_str);
    }

    const auto role_type_str = settings.get<std::string>(Settings::Path::kROLE);
    if (auto role_type = Utils::parse_role_type(role_type_str)) {
        cfg.role_type_ = *role_type;
    }
    else {
        Log::app()->warn(
            "Unrecognized role type, Got: {}, Expected: AUDIO/VIDEO, defaulting to NONE...",
            role_type_str);
    }

    return cfg;
}

std::unique_ptr<Api::ApiServer>
start_api_server_from_settings(asio::io_context& io_ctx, const Settings& settings, AppContext ctx) {
    const auto api_address = settings.get<std::string>(Settings::Path::kAPI_ADDRESS);
    const auto api_port = settings.get<unsigned short>(Settings::Path::kAPI_PORT);

    auto api = std::make_unique<Api::ApiServer>(io_ctx, api_address, api_port, ctx);
    api->run();

    Log::app()->info("Server ready. Send POST to http://{}:{}/stop to stop and save recording", api_address, api_port);

    return api;
}

} // namespace

struct App {
    asio::io_context io_ctx_{1};
    asio::signal_set signals_{io_ctx_, SIGINT, SIGTERM};

    Settings settings_;
    Platform::RecordingConfig config_;
    std::shared_ptr<Core::MediaRecorder> recorder_;

    std::unique_ptr<Sync::SyncMasterServer> sync_server_;
    std::unique_ptr<Sync::SyncWorkerClient> sync_client_;
    std::unique_ptr<Api::ApiServer> api_server_;

    explicit App(Settings settings, Platform::RecordingConfig config, std::shared_ptr<Core::MediaRecorder> recorder)
        : settings_(std::move(settings))
        , config_(std::move(config))
        , recorder_(std::move(recorder)) {}

    void run() {
        wire_role();
        wire_signals();

        io_ctx_.run();
        Log::app()->info("Shutdown complete");
    }

private:
    void wire_role() {
        const RoleType role = config_.role_type_;

        switch (role) {
        case RoleType::kAudio: wire_audio_worker(); break;
        case RoleType::kVideo: wire_video_master(); break;
        default: wire_standalone(); break;
        }
    }

    void wire_audio_worker() {
        const auto sync_peer_host = settings_.get<std::string>(Settings::Path::kSYNC_PEER_ADDRESS);
        const auto sync_port = settings_.get<int>(Settings::Path::kSYNC_PORT);

        sync_client_ = std::make_unique<Sync::SyncWorkerClient>(io_ctx_, sync_peer_host, sync_port, recorder_);
        sync_client_->start();
    }

    void wire_video_master() {
        const auto sync_bind_address = settings_.get<std::string>(Settings::Path::kSYNC_BIND_ADDRESS);
        const auto sync_port = settings_.get<int>(Settings::Path::kSYNC_PORT);

        sync_server_ = std::make_unique<Sync::SyncMasterServer>(io_ctx_, sync_bind_address, sync_port);
        if (auto res = sync_server_->start(); !res) {
            Log::crash_error("Failed initializing server");
        }

        sync_server_->enable_auto_start(recorder_, config_.segment_seconds_);
        Log::app()->info("Video role initialized as sync master");

        AppContext app_context{.server = sync_server_.get(), .recorder = nullptr};
        api_server_ = start_api_server_from_settings(io_ctx_, settings_, app_context);
    }
    void wire_standalone() {
        AppContext app_context{.server = nullptr, .recorder = recorder_.get()};
        api_server_ = start_api_server_from_settings(io_ctx_, settings_, app_context);

        if (auto res = recorder_->start(); !res) {
            Log::crash_error(std::format("Failed starting media recorder: {}", res.error().what()));
        }

        Log::app()->info("Recording started");
    }

    void wire_signals() {
        signals_.async_wait([this](const boost::system::error_code& errc, int signum) {
            if (errc) {
                return;
            }

            Log::app()->info(
                "Received signal {} ({}). Shutting down gracefully...",
                signum,
                signum == SIGINT ? "SIGINT" : "SIGTERM");

            shutdown();
        });
    }

    void shutdown() {
        const RoleType role = config_.role_type_;

        switch (role) {
        case RoleType::kAudio:
            if (sync_client_) {
                sync_client_->stop();
            }
            io_ctx_.stop();
            break;
        case RoleType::kVideo:
            if (sync_server_) {
                const int64_t master_ns = Sync::unix_now_ns();
                sync_server_->send_stop_at(master_ns);
            }
            io_ctx_.stop();
            break;
        default:
            if (recorder_->is_recording()) {
                Log::app()->info("Saving buffer...");
                if (auto res = recorder_->save_and_upload(); res) {
                    Log::app()->info("Recording saved successfully");
                }
                else {
                    Log::app()->error("Failed saving recording: {}", res.error().what());
                }

                Log::app()->info("Stopping recording...");
                recorder_->stop();
            }
            io_ctx_.stop();
            break;
        }
    }
};


int main(int argc, char** argv) {
    platform_init(argc, argv);

    auto settings_res = Settings::load("settings.toml");
    if (!settings_res) {
        Log::crash_error(std::format("Failed to load settings.toml: {}", settings_res.error().what()));
    }

    auto settings = std::move(settings_res.value());
    Log::app()->info("Settings toml:\n{}", settings.dump());

    init_logging_from_settings(settings);

    auto config = make_recording_config(settings);

    auto recorder = std::make_shared<Core::MediaRecorder>();
    if (auto res = recorder->initialize(config); !res) {
        Log::crash_error(std::format("Failed initializing media recorder: {}", res.error().what()));
    }

    App app(std::move(settings), std::move(config), std::move(recorder));
    app.run();
}
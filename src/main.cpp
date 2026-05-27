#include "Settings.hpp"
#include "api/ApiServer.hpp"
#include "core/MediaRecorder.hpp"
#include "core/RecordingConfig.hpp"

#include "utils/log.hpp"

using namespace VSCapture;

#ifdef WIN32
#include <consoleapi3.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <winuser.h>
#endif

namespace {

void platform_init(int argc, char **argv) {
#ifdef WIN32
  auto has_arg = [](int argc, char **argv, std::string_view key) {
    for (int i = 0; i < argc; ++i) {
      if (std::string_view(argv[i]) == key)
        return true;
    }
    return false;
  };

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

void init_logging_from_settings(const Settings &settings) {
  const auto max_log_size =
      settings.get<int>(Settings::Path::kMAX_LOG_SIZE_BYTES);
  const auto max_log_files = settings.get<int>(Settings::Path::kMAX_FILES);

  Log::init_logging(max_log_size, max_log_files);
  Log::set_log_level(settings.get<std::string>(Settings::Path::kLOG_LEVEL));
}

Core::RecordingConfig make_recording_config(const Settings &settings) {
  Core::RecordingConfig cfg;
  cfg.video.fps_ = settings.get<int>(Settings::Path::kFPS);
  cfg.video.recording_length_seconds_ =
      settings.get<int>(Settings::Path::kRECORDING_LENGTH_SECONDS);
  cfg.video.segment_buffer_seconds_ =
      settings.get<int>(Settings::Path::kSEGMENT_BUFFER_SECONDS);
  cfg.video.bitrate_ = settings.get<int>(Settings::Path::kBITRATE);

  cfg.audio.output_device_name_ =
      settings.get<std::string>(Settings::Path::kOUTPUT_DEVICE_NAME);
  cfg.audio.input_device_name_ =
      settings.get<std::string>(Settings::Path::kINPUT_DEVICE_NAME);

  return cfg;
}

} // namespace

int main(int argc, char **argv) {
  platform_init(argc, argv);

  auto settings_res = Settings::load("settings.toml");
  if (!settings_res) {
    Log::crash_error(std::format("Failed to load settings.toml: {}",
                                 settings_res.error().what()));
  }

  auto settings = std::move(settings_res.value());
  Log::app()->info("Settings toml:\n{}", settings.dump());

  init_logging_from_settings(settings);

  auto config = make_recording_config(settings);

  Core::MediaRecorder recorder;
  if (auto res = recorder.initialize(config); !res) {
    Log::crash_error(std::format("Failed initializing media recorder: {}",
                                 res.error().what()));
  }

  asio::io_context io_ctx{1};
  asio::signal_set signals{io_ctx, SIGINT, SIGTERM};

  const auto api_address =
      settings.get<std::string>(Settings::Path::kAPI_ADDRESS);
  const auto api_port = settings.get<unsigned short>(Settings::Path::kAPI_PORT);

  auto api_server_res =
      Api::ApiServer::create(io_ctx, api_address, api_port, &recorder);
  if (!api_server_res) {
    Log::crash_error(std::format("Failed starting API server: {}",
                                 api_server_res.error()
                                     .with_context("Failed creating API server")
                                     .what()));
  }

  auto api_server = std::move(api_server_res.value());
  api_server.run();

  Log::app()->info(
      "Server ready. Send POST to http://{}:{}/stop to stop and save recording",
      api_address, api_port);

  if (auto res = recorder.start(); !res) {
    Log::crash_error(
        std::format("Failed starting media recorder: {}", res.error().what()));
  }

  Log::app()->info("Recording started");

  signals.async_wait([&io_ctx, &recorder](const boost::system::error_code &errc,
                                          int signum) {
    if (errc) {
      return;
    }

    Log::app()->info("Received signal {} ({}). Shutting down gracefully...",
                     signum, signum == SIGINT ? "SIGINT" : "SIGTERM");

    if (recorder.is_recording()) {
      Log::app()->info("Saving buffer...");
      if (auto res = recorder.save_recording("recording.mp4"); res) {
        Log::app()->info("Recording saved successfully to {}", res.value());
      } else {
        Log::app()->error("Failed saving recording: {}", res.error().what());
      }

      Log::app()->info("Stopping recording...");
      recorder.stop();
    }
    io_ctx.stop();
  });

  io_ctx.run();
  Log::app()->info("Shutdown complete");
}

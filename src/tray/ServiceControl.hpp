#pragma once

namespace AVCapture::Tray {

// Launches the recorder via whatever autostart mechanism deploy.sh /
// deploy_task.ps1 registered it under. Graceful stop is not here -- it goes
// through ApiClient::request_shutdown() so the recorder saves its buffer
// before exiting, instead of relying on the service manager to deliver a
// stop signal (unreliable on Windows, see deploy notes).
class ServiceControl {
public:
  // Returns false only if the launcher command itself could not be spawned.
  // Does not guarantee the recorder actually came up -- poll ApiClient
  // afterwards for that.
  static bool start();
};

} // namespace AVCapture::Tray

#include "ServiceControl.hpp"

#include <QProcess>

namespace AVCapture::Tray {

bool ServiceControl::start() {
#ifdef _WIN32
  return QProcess::startDetached("schtasks",
                                 {"/run", "/tn", "AVCapture"});
#else
  return QProcess::startDetached("systemctl",
                                 {"--user", "start", "AVCapture.service"});
#endif
}

} // namespace AVCapture::Tray

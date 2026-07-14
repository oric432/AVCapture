#include "TrayIcon.hpp"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QtPlugin>

// Static Qt builds need this plugin linked in and its constructor forced to run.
// Shared/system Qt builds load platform plugins dynamically.
#ifdef AVCAPTURE_IMPORT_QXCB_PLUGIN
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QApplication::setQuitOnLastWindowClosed(false);

  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    QMessageBox::critical(nullptr, "AVCapture",
                          "No system tray available on this desktop.");
    return 1;
  }

  const QString settings_path =
      QDir(QApplication::applicationDirPath()).filePath("settings.toml");

  AVCapture::Tray::TrayIcon tray(settings_path);

  return QApplication::exec();
}

#pragma once

#include "ApiClient.hpp"

#include <QSystemTrayIcon>

class QMenu;
class QAction;
class QTimer;

namespace AVCapture::Tray {

class TrayIcon : public QSystemTrayIcon {
  Q_OBJECT

public:
  explicit TrayIcon(QString settings_path, QObject *parent = nullptr);

private slots:
  void on_start_clicked();
  void on_stop_clicked();
  void on_save_clicked();
  void on_edit_settings_clicked();
  void on_settings_saved();
  void on_quit_clicked();
  void poll_status();

private:
  void apply_status(bool reachable, bool recording);
  void wait_for_healthy_then(int attempts_left);

  QString settings_path_;
  ApiClient api_client_;

  QMenu *menu_{};
  QAction *start_action_{};
  QAction *stop_action_{};
  QAction *save_action_{};
  QAction *settings_action_{};
  QTimer *poll_timer_{};
};

} // namespace AVCapture::Tray

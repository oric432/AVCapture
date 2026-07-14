#include "TrayIcon.hpp"
#include "SettingsDialog.hpp"
#include "ServiceControl.hpp"

#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QTimer>

namespace AVCapture::Tray {

namespace {

struct ApiEndpoint {
  std::string address = "127.0.0.1";
  unsigned short port = 8084;
  std::string api_key;
};

ApiEndpoint read_api_endpoint(const QString &settings_path) {
  ApiEndpoint endpoint;

  auto file_res = toml::parse_file(settings_path.toStdString());
  if (file_res) {
    endpoint.address =
        file_res.table().at_path("api.address").value_or(endpoint.address);
    endpoint.port =
        file_res.table().at_path("api.port").value_or<int>(endpoint.port);
    endpoint.api_key =
        file_res.table().at_path("api.api_key").value_or(std::string{});
  }
  return endpoint;
}

QIcon make_dot_icon(const QColor &color) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(color);
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(4, 4, 24, 24);
  return QIcon(pixmap);
}

} // namespace

TrayIcon::TrayIcon(QString settings_path, QObject *parent)
    : QSystemTrayIcon(parent), settings_path_(std::move(settings_path)),
      api_client_([&] {
        auto endpoint = read_api_endpoint(settings_path_);
        return ApiClient(std::move(endpoint.address), endpoint.port,
                         std::move(endpoint.api_key));
      }()) {
  menu_ = new QMenu();

  start_action_ = menu_->addAction("Start Recording");
  connect(start_action_, &QAction::triggered, this,
          &TrayIcon::on_start_clicked);

  stop_action_ = menu_->addAction("Stop Recording");
  connect(stop_action_, &QAction::triggered, this, &TrayIcon::on_stop_clicked);

  save_action_ = menu_->addAction("Save Recording");
  connect(save_action_, &QAction::triggered, this, &TrayIcon::on_save_clicked);

  menu_->addSeparator();

  settings_action_ = menu_->addAction("Edit Settings...");
  connect(settings_action_, &QAction::triggered, this,
          &TrayIcon::on_edit_settings_clicked);

  menu_->addSeparator();
  connect(menu_->addAction("Quit"), &QAction::triggered, this,
          &TrayIcon::on_quit_clicked);

  setContextMenu(menu_);
  setIcon(make_dot_icon(Qt::gray));
  setToolTip("AVCapture: checking status...");
  show();

  poll_timer_ = new QTimer(this);
  connect(poll_timer_, &QTimer::timeout, this, &TrayIcon::poll_status);
  poll_timer_->start(3000);
  poll_status();
}

void TrayIcon::apply_status(bool reachable, bool recording) {
  start_action_->setEnabled(!reachable);
  stop_action_->setEnabled(reachable);
  save_action_->setEnabled(reachable && recording);

  if (!reachable) {
    setIcon(make_dot_icon(Qt::gray));
    setToolTip("AVCapture: stopped");
  } else if (recording) {
    setIcon(make_dot_icon(Qt::green));
    setToolTip("AVCapture: recording");
  } else {
    setIcon(make_dot_icon(Qt::yellow));
    setToolTip("AVCapture: running, not recording");
  }
}

void TrayIcon::poll_status() {
  auto status = api_client_.get_status();
  apply_status(status.has_value(), status && status->recording);
}

void TrayIcon::wait_for_healthy_then(int attempts_left) {
  if (attempts_left <= 0) {
    QMessageBox::warning(nullptr, "AVCapture",
                         "AVCapture failed to start -- check logs.");
    poll_status();
    return;
  }

  if (api_client_.is_healthy()) {
    poll_status();
    return;
  }

  QTimer::singleShot(500, this, [this, attempts_left] {
    wait_for_healthy_then(attempts_left - 1);
  });
}

void TrayIcon::on_start_clicked() {
  if (!ServiceControl::start()) {
    QMessageBox::warning(nullptr, "AVCapture",
                         "Failed to launch AVCapture.");
    return;
  }
  wait_for_healthy_then(16); // ~8s at 500ms intervals
}

void TrayIcon::on_stop_clicked() {
  api_client_.request_shutdown();
  QTimer::singleShot(500, this, [this] { poll_status(); });
}

void TrayIcon::on_save_clicked() {
  if (!api_client_.save_recording()) {
    QMessageBox::warning(nullptr, "AVCapture", "Failed to save recording.");
  }
}

void TrayIcon::on_edit_settings_clicked() {
  auto *dialog = new SettingsDialog(settings_path_);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &SettingsDialog::settingsSaved, this,
          &TrayIcon::on_settings_saved);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void TrayIcon::on_settings_saved() {
  auto status = api_client_.get_status();
  if (!status) {
    // Not running -- settings.toml is only read at startup, so there's
    // nothing to restart. Next manual Start picks up the new values.
    return;
  }

  const auto reply = QMessageBox::question(
      nullptr, "AVCapture",
      "Applying these settings will restart recording now. Continue?");
  if (reply != QMessageBox::Yes) {
    return;
  }

  api_client_.request_shutdown();
  QTimer::singleShot(500, this, [this] {
    if (!ServiceControl::start()) {
      QMessageBox::warning(nullptr, "AVCapture",
                           "Failed to restart AVCapture.");
      return;
    }
    wait_for_healthy_then(16);
  });
}

void TrayIcon::on_quit_clicked() {
  // Only the tray exits -- the recorder is a separate autostart entry and
  // keeps running.
  qApp->quit();
}

} // namespace AVCapture::Tray

#include "SettingsDialog.hpp"

#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>
#include <fstream>
#include <sstream>

namespace AVCapture::Tray {

namespace {
constexpr std::array<const char *, 7> kLogLevels = {
    "trace", "debug", "info", "warn", "error", "critical", "off"};
}

SettingsDialog::SettingsDialog(QString settings_path, QWidget *parent)
    : QDialog(parent), settings_path_(std::move(settings_path)) {
  setWindowTitle("AVCapture Settings");

  auto *form = new QFormLayout();

  api_address_ = new QLineEdit(this);
  form->addRow("API address", api_address_);

  api_port_ = new QSpinBox(this);
  api_port_->setRange(1, 65535);
  form->addRow("API port", api_port_);

  fps_ = new QSpinBox(this);
  fps_->setRange(1, 240);
  form->addRow("FPS", fps_);

  bitrate_ = new QSpinBox(this);
  bitrate_->setRange(1, 100'000'000);
  form->addRow("Bitrate (bps)", bitrate_);

  recording_length_seconds_ = new QSpinBox(this);
  recording_length_seconds_->setRange(1, 3600);
  form->addRow("Recording length (s)", recording_length_seconds_);

  segment_buffer_seconds_ = new QSpinBox(this);
  segment_buffer_seconds_->setRange(1, 3600);
  form->addRow("Segment buffer (s)", segment_buffer_seconds_);

  output_device_name_ = new QLineEdit(this);
  form->addRow("Audio output device", output_device_name_);

  input_device_name_ = new QLineEdit(this);
  form->addRow("Audio input device", input_device_name_);

  log_level_ = new QComboBox(this);
  for (const auto *level : kLogLevels) {
    log_level_->addItem(level);
  }
  form->addRow("Log level", log_level_);

  max_log_size_bytes_ = new QSpinBox(this);
  max_log_size_bytes_->setRange(1, 1'000'000'000);
  form->addRow("Max log size (bytes)", max_log_size_bytes_);

  max_files_ = new QSpinBox(this);
  max_files_->setRange(1, 1000);
  form->addRow("Max log files", max_files_);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save |
                                       QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &SettingsDialog::on_save_clicked);
  connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);

  load();
}

void SettingsDialog::load() {
  auto file_res = toml::parse_file(settings_path_.toStdString());
  if (!file_res) {
    QMessageBox::warning(this, "AVCapture Settings",
                         QString("Failed reading %1: %2")
                             .arg(settings_path_)
                             .arg(QString::fromUtf8(
                                 file_res.error().description().data(),
                                 static_cast<int>(
                                     file_res.error().description().size()))));
    return;
  }

  const toml::table &t = file_res.table();

  api_address_->setText(QString::fromStdString(
      t.at_path("api.address").value_or(std::string{"127.0.0.1"})));
  api_port_->setValue(t.at_path("api.port").value_or(8084));
  fps_->setValue(t.at_path("recording.fps").value_or(30));
  bitrate_->setValue(t.at_path("recording.bitrate").value_or(4000000));
  recording_length_seconds_->setValue(
      t.at_path("recording.recording_length_seconds").value_or(10));
  segment_buffer_seconds_->setValue(
      t.at_path("recording.segment_buffer_seconds").value_or(2));
  output_device_name_->setText(QString::fromStdString(
      t.at_path("audio.output_device_name").value_or(std::string{})));
  input_device_name_->setText(QString::fromStdString(
      t.at_path("audio.input_device_name").value_or(std::string{})));

  const auto level =
      t.at_path("log.level").value_or(std::string{"info"});
  if (int idx = log_level_->findText(QString::fromStdString(level));
      idx >= 0) {
    log_level_->setCurrentIndex(idx);
  }
  max_log_size_bytes_->setValue(
      t.at_path("log.max_log_size_bytes").value_or(5242880));
  max_files_->setValue(t.at_path("log.max_files").value_or(5));
}

void SettingsDialog::on_save_clicked() {
  toml::table api_tbl;
  api_tbl.insert("address", api_address_->text().toStdString());
  api_tbl.insert("port", api_port_->value());

  toml::table recording_tbl;
  recording_tbl.insert("fps", fps_->value());
  recording_tbl.insert("bitrate", bitrate_->value());
  recording_tbl.insert("recording_length_seconds",
                       recording_length_seconds_->value());
  recording_tbl.insert("segment_buffer_seconds",
                       segment_buffer_seconds_->value());

  toml::table audio_tbl;
  audio_tbl.insert("output_device_name", output_device_name_->text().toStdString());
  audio_tbl.insert("input_device_name", input_device_name_->text().toStdString());

  toml::table log_tbl;
  log_tbl.insert("level", log_level_->currentText().toStdString());
  log_tbl.insert("max_log_size_bytes", max_log_size_bytes_->value());
  log_tbl.insert("max_files", max_files_->value());

  toml::table root;
  root.insert("api", std::move(api_tbl));
  root.insert("recording", std::move(recording_tbl));
  root.insert("audio", std::move(audio_tbl));
  root.insert("log", std::move(log_tbl));

  std::ofstream out(settings_path_.toStdString(), std::ios::trunc);
  if (!out) {
    QMessageBox::warning(this, "AVCapture Settings",
                         QString("Failed writing %1").arg(settings_path_));
    return;
  }
  out << root;
  out.close();

  emit settingsSaved();
  accept();
}

} // namespace AVCapture::Tray

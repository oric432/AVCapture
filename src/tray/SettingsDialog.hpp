#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QSpinBox;
class QComboBox;

namespace AVCapture::Tray {

// Structured form for the 10 keys in settings.toml (api/recording/audio/log
// sections, matching settings-example.toml). Writing the file is all this
// dialog does -- deciding whether a restart is needed lives in TrayIcon.
class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(QString settings_path, QWidget *parent = nullptr);

signals:
  void settingsSaved();

private slots:
  void on_save_clicked();

private:
  void load();

  QString settings_path_;

  QLineEdit *api_address_{};
  QSpinBox *api_port_{};
  QSpinBox *fps_{};
  QSpinBox *bitrate_{};
  QSpinBox *recording_length_seconds_{};
  QSpinBox *segment_buffer_seconds_{};
  QLineEdit *output_device_name_{};
  QLineEdit *input_device_name_{};
  QComboBox *log_level_{};
  QSpinBox *max_log_size_bytes_{};
  QSpinBox *max_files_{};
};

} // namespace AVCapture::Tray

#pragma once

/**
 * @brief App Settings Widget
 *
 * Global settings for the GW2 AIO Manager including:
 * - GW2 Path configuration
 * - Network settings
 * - Cinema mode
 * - Crash handling
 * - General preferences
 *
 * DO NOT ADD:
 * - Inline implementations (use SettingsWidget.cpp)
 */

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/ToggleSwitch.h"
#include "ui/UIHelpers.h"

class DataService; // Forward declaration

class SettingsWidget : public QWidget {
  Q_OBJECT

public:
  explicit SettingsWidget(DataService *dataService, QWidget *parent = nullptr);

  QString gw2Path() const { return m_gw2PathEdit->text(); }
  void setGw2Path(const QString &path) { m_gw2PathEdit->setText(path); }

signals:
  void gw2PathChanged(const QString &path);
  void settingsChanged();

private slots:
  void onBrowseGw2Path();
  void onReset();

private:
  void setupUI();
  void loadSettings();
  void autoSave();
  void showSavedIndicator();

  // Path settings
  DataService *m_dataService;
  QLineEdit *m_gw2PathEdit;

  // Launch settings
  LabeledToggle *m_startMinimizedToggle;

  // Misc
  LabeledToggle *m_checkUpdatesToggle;
  LabeledToggle *m_showTrayIconToggle;
  LabeledToggle *m_cefCleanupToggle;

  // Theme
  QComboBox *m_themeCombo;

  // Saved indicator
  QLabel *m_savedLabel;
  QGraphicsOpacityEffect *m_savedOpacity;
  QPropertyAnimation *m_savedFadeIn;
  QPropertyAnimation *m_savedFadeOut;
  QTimer *m_saveDebounceTimer;
  QTimer *m_savedHoldTimer;
};

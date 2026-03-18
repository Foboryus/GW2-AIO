#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "core/SettingsManager.h"

/**
 * @brief Settings UI page with all configuration options
 *
 * DO NOT ADD:
 * - Inline implementations (use SettingsPage.cpp)
 * - Core business logic (belongs in SettingsManager)
 */
class SettingsPage : public QWidget {
  Q_OBJECT

public:
  explicit SettingsPage(SettingsManager *settings, QWidget *parent = nullptr);

signals:
  void settingsChanged();
  void restartRequired();

private slots:
  void onSaveClicked();
  void onResetClicked();
  void onExportClicked();
  void onImportClicked();

private:
  void setupUI();
  void loadSettings();
  void saveSettings();

  QWidget *createGeneralTab();
  QWidget *createRadialTab();
  QWidget *createDPSTab();
  QWidget *createMarkersTab();
  QWidget *createModulesTab();
  QWidget *createAdvancedTab();

  SettingsManager *m_settings;

  // General settings
  QCheckBox *m_startMinimized;
  QCheckBox *m_startWithWindows;
  QCheckBox *m_checkUpdates;
  QComboBox *m_language;
  QComboBox *m_theme;

  // Radial settings
  QCheckBox *m_radialEnabled;
  QSlider *m_radialOpacity;
  QSpinBox *m_radialSize;
  QCheckBox *m_radialAnimations;

  // DPS settings
  QCheckBox *m_dpsEnabled;
  QComboBox *m_dpsSource;
  QCheckBox *m_dpsShowGraph;
  QSpinBox *m_dpsGraphDuration;
  QSlider *m_dpsOpacity;

  // Marker settings
  QCheckBox *m_markersEnabled;
  QSlider *m_markerOpacity;
  QSpinBox *m_markerFadeNear;
  QSpinBox *m_markerFadeFar;
  QCheckBox *m_showTrails;

  // Module settings
  QCheckBox *m_modulesEnabled;
  QLineEdit *m_modulesPath;

  // Advanced
  QCheckBox *m_debugMode;
  QCheckBox *m_consoleLog;
  QLineEdit *m_gw2Path;
};

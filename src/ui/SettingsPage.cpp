/**
 * @file SettingsPage.cpp
 * @brief Settings UI page with all configuration options
 *
 * This file contains the implementation of the SettingsPage class which
 * provides a tabbed interface for all application settings (General, Radial
 * Menus, DPS Tracker, Markers, Modules, and Advanced).
 *
 * DO NOT ADD:
 * - Core business logic (belongs in SettingsManager)
 * - Non-UI settings validation
 * - Direct file I/O (use SettingsManager)
 */

#include "SettingsPage.h"
#include "UIHelpers.h"
#include "core/ThemeManager.h"

SettingsPage::SettingsPage(SettingsManager *settings, QWidget *parent)
    : QWidget(parent), m_settings(settings) {
  setupUI();
  loadSettings();
}

void SettingsPage::setupUI() {
  // No inline setStyleSheet — all styling handled by ThemeManager global QSS

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Tab widget
  QTabWidget *tabs = new QTabWidget();
  tabs->addTab(createGeneralTab(), "General");
  tabs->addTab(createRadialTab(), "Radial Menus");
  tabs->addTab(createDPSTab(), "DPS Tracker");
  tabs->addTab(createMarkersTab(), "Markers");
  tabs->addTab(createModulesTab(), "Modules");
  tabs->addTab(createAdvancedTab(), "Advanced");

  mainLayout->addWidget(tabs);

  // Buttons
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setContentsMargins(16, 8, 16, 16);

  QPushButton *exportBtn = new QPushButton("Export Settings");
  UIHelpers::applyNeutralStyle(exportBtn);
  connect(exportBtn, &QPushButton::clicked, this,
          &SettingsPage::onExportClicked);
  buttonLayout->addWidget(exportBtn);

  QPushButton *importBtn = new QPushButton("Import Settings");
  UIHelpers::applyNeutralStyle(importBtn);
  connect(importBtn, &QPushButton::clicked, this,
          &SettingsPage::onImportClicked);
  buttonLayout->addWidget(importBtn);

  buttonLayout->addStretch();

  QPushButton *resetBtn = new QPushButton("Reset to Defaults");
  UIHelpers::applyCancelStyle(resetBtn);
  connect(resetBtn, &QPushButton::clicked, this, &SettingsPage::onResetClicked);
  buttonLayout->addWidget(resetBtn);

  QPushButton *saveBtn = new QPushButton("Save");
  UIHelpers::applyPrimaryStyle(saveBtn);
  connect(saveBtn, &QPushButton::clicked, this, &SettingsPage::onSaveClicked);
  buttonLayout->addWidget(saveBtn);

  mainLayout->addLayout(buttonLayout);
}

QWidget *SettingsPage::createGeneralTab() {
  QWidget *tab = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(tab);
  layout->setSpacing(16);

  // Startup group
  QGroupBox *startupGroup = new QGroupBox("Startup");
  QVBoxLayout *startupLayout = new QVBoxLayout(startupGroup);

  m_startMinimized = new QCheckBox("Start minimized to tray");
  startupLayout->addWidget(m_startMinimized);

  m_startWithWindows = new QCheckBox("Start with Windows");
  startupLayout->addWidget(m_startWithWindows);

  m_checkUpdates = new QCheckBox("Check for updates on startup");
  startupLayout->addWidget(m_checkUpdates);

  layout->addWidget(startupGroup);

  // Appearance group
  QGroupBox *appearanceGroup = new QGroupBox("Appearance");
  QFormLayout *appearanceLayout = new QFormLayout(appearanceGroup);

  m_theme = new QComboBox();
  for (auto t : ThemeManager::builtinThemes()) {
    m_theme->addItem(ThemeManager::themeName(t), static_cast<int>(t));
  }
  appearanceLayout->addRow("Theme:", m_theme);

  // Live theme preview on selection change
  connect(m_theme, &QComboBox::currentIndexChanged, this, [this](int idx) {
    auto themes = ThemeManager::builtinThemes();
    if (idx >= 0 && idx < themes.size()) {
      ThemeManager::instance().setBuiltinTheme(themes.at(idx));
    }
  });

  m_language = new QComboBox();
  m_language->addItems({"English", "Deutsch", "Français", "Español"});
  appearanceLayout->addRow("Language:", m_language);

  layout->addWidget(appearanceGroup);
  layout->addStretch();

  return tab;
}

QWidget *SettingsPage::createRadialTab() {
  QWidget *tab = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(tab);
  layout->setSpacing(16);

  m_radialEnabled = new QCheckBox("Enable radial menus");
  layout->addWidget(m_radialEnabled);

  QGroupBox *group = new QGroupBox("Appearance");
  QFormLayout *form = new QFormLayout(group);

  m_radialOpacity = new QSlider(Qt::Horizontal);
  m_radialOpacity->setRange(10, 100);
  form->addRow("Opacity:", m_radialOpacity);

  m_radialSize = new QSpinBox();
  m_radialSize->setRange(200, 600);
  m_radialSize->setSuffix(" px");
  form->addRow("Size:", m_radialSize);

  m_radialAnimations = new QCheckBox("Enable animations");
  form->addRow("", m_radialAnimations);

  layout->addWidget(group);
  layout->addStretch();

  return tab;
}

QWidget *SettingsPage::createDPSTab() {
  QWidget *tab = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(tab);
  layout->setSpacing(16);

  m_dpsEnabled = new QCheckBox("Enable DPS tracker");
  layout->addWidget(m_dpsEnabled);

  QGroupBox *group = new QGroupBox("Data Source");
  QFormLayout *form = new QFormLayout(group);

  m_dpsSource = new QComboBox();
  m_dpsSource->addItems(
      {"Mumble Link (estimated)", "ArcDPS Logs", "ArcDPS Real-Time"});
  form->addRow("Source:", m_dpsSource);

  layout->addWidget(group);

  QGroupBox *displayGroup = new QGroupBox("Display");
  QFormLayout *displayForm = new QFormLayout(displayGroup);

  m_dpsShowGraph = new QCheckBox("Show damage graph");
  displayForm->addRow("", m_dpsShowGraph);

  m_dpsGraphDuration = new QSpinBox();
  m_dpsGraphDuration->setRange(5, 60);
  m_dpsGraphDuration->setSuffix(" seconds");
  displayForm->addRow("Graph duration:", m_dpsGraphDuration);

  m_dpsOpacity = new QSlider(Qt::Horizontal);
  m_dpsOpacity->setRange(10, 100);
  displayForm->addRow("Opacity:", m_dpsOpacity);

  layout->addWidget(displayGroup);
  layout->addStretch();

  return tab;
}

QWidget *SettingsPage::createMarkersTab() {
  QWidget *tab = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(tab);
  layout->setSpacing(16);

  m_markersEnabled = new QCheckBox("Enable marker system");
  layout->addWidget(m_markersEnabled);

  QGroupBox *group = new QGroupBox("Rendering");
  QFormLayout *form = new QFormLayout(group);

  m_markerOpacity = new QSlider(Qt::Horizontal);
  m_markerOpacity->setRange(10, 100);
  form->addRow("Opacity:", m_markerOpacity);

  m_markerFadeNear = new QSpinBox();
  m_markerFadeNear->setRange(0, 1000);
  m_markerFadeNear->setSuffix(" units");
  form->addRow("Fade near:", m_markerFadeNear);

  m_markerFadeFar = new QSpinBox();
  m_markerFadeFar->setRange(100, 5000);
  m_markerFadeFar->setSuffix(" units");
  form->addRow("Fade far:", m_markerFadeFar);

  m_showTrails = new QCheckBox("Show trails");
  form->addRow("", m_showTrails);

  layout->addWidget(group);
  layout->addStretch();

  return tab;
}

QWidget *SettingsPage::createModulesTab() {
  QWidget *tab = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(tab);
  layout->setSpacing(16);

  m_modulesEnabled = new QCheckBox("Enable Blish-HUD modules");
  layout->addWidget(m_modulesEnabled);

  QLabel *note = new QLabel("Requires .NET 6+ Runtime installed");
  UIHelpers::applyWarningColorRole(note);
  layout->addWidget(note);

  QGroupBox *group = new QGroupBox("Paths");
  QFormLayout *form = new QFormLayout(group);

  QHBoxLayout *pathLayout = new QHBoxLayout();
  m_modulesPath = new QLineEdit();
  pathLayout->addWidget(m_modulesPath);
  QPushButton *browseBtn = new QPushButton("Browse...");
  UIHelpers::applyNeutralStyle(browseBtn);
  connect(browseBtn, &QPushButton::clicked, this, [this]() {
    QString dir =
        QFileDialog::getExistingDirectory(this, "Select Modules Directory");
    if (!dir.isEmpty())
      m_modulesPath->setText(dir);
  });
  pathLayout->addWidget(browseBtn);
  form->addRow("Modules folder:", pathLayout);

  layout->addWidget(group);
  layout->addStretch();

  return tab;
}

QWidget *SettingsPage::createAdvancedTab() {
  QWidget *tab = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(tab);
  layout->setSpacing(16);

  QGroupBox *group = new QGroupBox("Debug");
  QVBoxLayout *groupLayout = new QVBoxLayout(group);

  m_debugMode = new QCheckBox("Enable debug mode");
  groupLayout->addWidget(m_debugMode);

  m_consoleLog = new QCheckBox("Show console window");
  groupLayout->addWidget(m_consoleLog);

  layout->addWidget(group);

  QGroupBox *pathsGroup = new QGroupBox("Game Path");
  QFormLayout *pathsForm = new QFormLayout(pathsGroup);

  QHBoxLayout *gw2Layout = new QHBoxLayout();
  m_gw2Path = new QLineEdit();
  gw2Layout->addWidget(m_gw2Path);
  QPushButton *browsegw2 = new QPushButton("Browse...");
  UIHelpers::applyNeutralStyle(browsegw2);
  connect(browsegw2, &QPushButton::clicked, this, [this]() {
    QString file = QFileDialog::getOpenFileName(
        this, "Select Gw2-64.exe", QString(), "Executables (*.exe)");
    if (!file.isEmpty())
      m_gw2Path->setText(file);
  });
  gw2Layout->addWidget(browsegw2);
  pathsForm->addRow("GW2 Executable:", gw2Layout);

  layout->addWidget(pathsGroup);
  layout->addStretch();

  return tab;
}

void SettingsPage::loadSettings() {
  // General settings
  m_startMinimized->setChecked(
      m_settings->value("general/startMinimized", false).toBool());
  m_startWithWindows->setChecked(
      m_settings->value("general/startWithWindows", false).toBool());
  m_checkUpdates->setChecked(
      m_settings->value("general/checkUpdates", true).toBool());
  m_language->setCurrentText(
      m_settings->value("general/language", "English").toString());
  int savedTheme = m_settings->value("general/selectedTheme", 0).toInt();
  m_theme->setCurrentIndex(qBound(0, savedTheme, m_theme->count() - 1));

  // Radial settings
  m_radialEnabled->setChecked(
      m_settings->value("radial/enabled", true).toBool());
  m_radialOpacity->setValue(m_settings->value("radial/opacity", 90).toInt());
  m_radialSize->setValue(m_settings->value("radial/size", 400).toInt());
  m_radialAnimations->setChecked(
      m_settings->value("radial/animations", true).toBool());

  // DPS settings
  m_dpsEnabled->setChecked(m_settings->value("dps/enabled", true).toBool());
  m_dpsSource->setCurrentText(
      m_settings->value("dps/source", "ArcDPS").toString());
  m_dpsShowGraph->setChecked(m_settings->value("dps/showGraph", true).toBool());
  m_dpsGraphDuration->setValue(
      m_settings->value("dps/graphDuration", 30).toInt());
  m_dpsOpacity->setValue(m_settings->value("dps/opacity", 80).toInt());

  // Marker settings
  m_markersEnabled->setChecked(
      m_settings->value("markers/enabled", true).toBool());
  m_markerOpacity->setValue(m_settings->value("markers/opacity", 100).toInt());
  m_markerFadeNear->setValue(
      m_settings->value("markers/fadeNear", 100).toInt());
  m_markerFadeFar->setValue(m_settings->value("markers/fadeFar", 800).toInt());
  m_showTrails->setChecked(
      m_settings->value("markers/showTrails", true).toBool());

  // Module settings
  m_modulesEnabled->setChecked(
      m_settings->value("modules/enabled", true).toBool());
  m_modulesPath->setText(m_settings->value("modules/path", "").toString());

  // Advanced settings
  m_debugMode->setChecked(
      m_settings->value("advanced/debugMode", false).toBool());
  m_consoleLog->setChecked(
      m_settings->value("advanced/consoleLog", false).toBool());
  m_gw2Path->setText(m_settings->value("gw2Path", "").toString());
}

void SettingsPage::saveSettings() {
  // General settings
  m_settings->setValue("general/startMinimized", m_startMinimized->isChecked());
  m_settings->setValue("general/startWithWindows",
                       m_startWithWindows->isChecked());
  m_settings->setValue("general/checkUpdates", m_checkUpdates->isChecked());
  m_settings->setValue("general/language", m_language->currentText());
  m_settings->setValue("general/selectedTheme", m_theme->currentIndex());

  // Radial settings
  m_settings->setValue("radial/enabled", m_radialEnabled->isChecked());
  m_settings->setValue("radial/opacity", m_radialOpacity->value());
  m_settings->setValue("radial/size", m_radialSize->value());
  m_settings->setValue("radial/animations", m_radialAnimations->isChecked());

  // DPS settings
  m_settings->setValue("dps/enabled", m_dpsEnabled->isChecked());
  m_settings->setValue("dps/source", m_dpsSource->currentText());
  m_settings->setValue("dps/showGraph", m_dpsShowGraph->isChecked());
  m_settings->setValue("dps/graphDuration", m_dpsGraphDuration->value());
  m_settings->setValue("dps/opacity", m_dpsOpacity->value());

  // Marker settings
  m_settings->setValue("markers/enabled", m_markersEnabled->isChecked());
  m_settings->setValue("markers/opacity", m_markerOpacity->value());
  m_settings->setValue("markers/fadeNear", m_markerFadeNear->value());
  m_settings->setValue("markers/fadeFar", m_markerFadeFar->value());
  m_settings->setValue("markers/showTrails", m_showTrails->isChecked());

  // Module settings
  m_settings->setValue("modules/enabled", m_modulesEnabled->isChecked());
  m_settings->setValue("modules/path", m_modulesPath->text());

  // Advanced settings
  m_settings->setValue("advanced/debugMode", m_debugMode->isChecked());
  m_settings->setValue("advanced/consoleLog", m_consoleLog->isChecked());
  m_settings->setValue("gw2Path", m_gw2Path->text());

  m_settings->sync();
}

void SettingsPage::onSaveClicked() {
  saveSettings();
  emit settingsChanged();
}

void SettingsPage::onResetClicked() {
  // Reset to defaults - General
  m_startMinimized->setChecked(false);
  m_startWithWindows->setChecked(false);
  m_checkUpdates->setChecked(true);
  m_language->setCurrentIndex(0); // First item (English)
  m_theme->setCurrentIndex(0);    // First item (Dark)

  // Radial
  m_radialEnabled->setChecked(true);
  m_radialOpacity->setValue(90);
  m_radialSize->setValue(400);
  m_radialAnimations->setChecked(true);

  // DPS
  m_dpsEnabled->setChecked(true);
  m_dpsSource->setCurrentIndex(0); // First item (ArcDPS)
  m_dpsShowGraph->setChecked(true);
  m_dpsGraphDuration->setValue(30);
  m_dpsOpacity->setValue(80);

  // Markers
  m_markersEnabled->setChecked(true);
  m_markerOpacity->setValue(100);
  m_markerFadeNear->setValue(100);
  m_markerFadeFar->setValue(800);
  m_showTrails->setChecked(true);

  // Modules
  m_modulesEnabled->setChecked(true);
  // m_modulesPath not reset (user-specific)

  // Advanced
  m_debugMode->setChecked(false);
  m_consoleLog->setChecked(false);
  // m_gw2Path not reset (user-specific)
}

void SettingsPage::onExportClicked() {
  QString path = QFileDialog::getSaveFileName(
      this, "Export Settings", "gw2aio_settings.json", "JSON Files (*.json)");
  if (!path.isEmpty()) {
    m_settings->exportToFile(path);
  }
}

void SettingsPage::onImportClicked() {
  QString path = QFileDialog::getOpenFileName(this, "Import Settings",
                                              QString(), "JSON Files (*.json)");
  if (!path.isEmpty()) {
    m_settings->importFromFile(path);
    loadSettings();
    emit settingsChanged();
  }
}

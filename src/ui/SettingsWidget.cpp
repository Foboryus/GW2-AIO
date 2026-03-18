/**
 * @file SettingsWidget.cpp
 * @brief App Settings Widget implementation
 *
 * Global settings for the GW2 AIO Manager including GW2 Path configuration,
 * network settings, cinema mode, crash handling, and general preferences.
 *
 * All settings auto-save instantly on change — no Save button needed.
 *
 * DO NOT ADD:
 * - Core settings persistence logic (use QSettings)
 * - Non-UI related functionality
 */

#include "SettingsWidget.h"
#include "core/CefManager.h"
#include "core/DataService.h"
#include "core/ThemeManager.h"

SettingsWidget::SettingsWidget(DataService *dataService, QWidget *parent)
    : QWidget(parent), m_dataService(dataService) {
  setupUI();
  loadSettings();
}

void SettingsWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);

  // Header
  auto *header = UIHelpers::createPageHeader(this, "Preferences", "tool");
  mainLayout->addWidget(header);

  // Inline "Saved" indicator in the header
  auto *headerLayout = qobject_cast<QHBoxLayout *>(header->layout());
  m_savedLabel = new QLabel();
  UIHelpers::setThemedPixmap(m_savedLabel, "check-circle", 16);
  m_savedLabel->setToolTip("Settings saved");
  m_savedLabel->setStyleSheet("background: transparent; border: none;");
  m_savedOpacity = new QGraphicsOpacityEffect(m_savedLabel);
  m_savedOpacity->setOpacity(0.0);
  m_savedLabel->setGraphicsEffect(m_savedOpacity);
  headerLayout->addWidget(m_savedLabel);

  // Fade animations
  m_savedFadeIn = new QPropertyAnimation(m_savedOpacity, "opacity", this);
  m_savedFadeIn->setDuration(200);
  m_savedFadeIn->setStartValue(0.0);
  m_savedFadeIn->setEndValue(1.0);

  m_savedFadeOut = new QPropertyAnimation(m_savedOpacity, "opacity", this);
  m_savedFadeOut->setDuration(400);
  m_savedFadeOut->setStartValue(1.0);
  m_savedFadeOut->setEndValue(0.0);

  // Hold timer — keeps indicator visible before fading out
  m_savedHoldTimer = new QTimer(this);
  m_savedHoldTimer->setSingleShot(true);
  m_savedHoldTimer->setInterval(2500);
  connect(m_savedHoldTimer, &QTimer::timeout, this,
          [this]() { m_savedFadeOut->start(); });

  // Debounce timer — prevents rapid-fire indicator flashes
  m_saveDebounceTimer = new QTimer(this);
  m_saveDebounceTimer->setSingleShot(true);
  m_saveDebounceTimer->setInterval(300);
  connect(m_saveDebounceTimer, &QTimer::timeout, this,
          &SettingsWidget::showSavedIndicator);

  // == GW2 Path ==
  auto *pathGroup = new QGroupBox("GW2 Installation");
  auto *pathLayout = new QVBoxLayout(pathGroup);

  auto *pathRow = new QHBoxLayout();
  m_gw2PathEdit = new QLineEdit();
  m_gw2PathEdit->setPlaceholderText("C:\\Program Files\\Guild Wars 2");
  m_gw2PathEdit->setReadOnly(true);
  pathRow->addWidget(m_gw2PathEdit);

  auto *browseBtn = new QPushButton("Browse...");
  UIHelpers::setThemedIcon(browseBtn, "folder");
  UIHelpers::applyNeutralStyle(browseBtn);
  connect(browseBtn, &QPushButton::clicked, this,
          &SettingsWidget::onBrowseGw2Path);
  pathRow->addWidget(browseBtn);
  pathLayout->addLayout(pathRow);

  auto *pathNote = new QLabel("Select the folder containing Gw2-64.exe");
  UIHelpers::applyHintRole(pathNote);
  pathLayout->addWidget(pathNote);
  mainLayout->addWidget(pathGroup);

  // == Launch Behavior ==
  auto *launchGroup = new QGroupBox("Launch Behavior");
  auto *launchLayout = new QVBoxLayout(launchGroup);

  m_showTrayIconToggle = new LabeledToggle("Show system tray icon");
  m_showTrayIconToggle->setChecked(true);
  launchLayout->addWidget(m_showTrayIconToggle);

  m_startMinimizedToggle = new LabeledToggle("Minimize to tray on AIO opening");
  launchLayout->addWidget(m_startMinimizedToggle);

  // Tray icon → start minimized interlock + auto-save
  connect(m_showTrayIconToggle, &LabeledToggle::toggled, [this](bool checked) {
    if (!checked) {
      m_startMinimizedToggle->setChecked(false);
      m_startMinimizedToggle->setEnabled(false);
    } else {
      m_startMinimizedToggle->setEnabled(true);
    }
    m_dataService->setShowTrayIcon(checked);
    autoSave();
  });

  connect(m_startMinimizedToggle, &LabeledToggle::toggled,
          [this](bool checked) {
            if (checked) {
              m_showTrayIconToggle->setChecked(true);
              m_showTrayIconToggle->setEnabled(false);
            } else {
              m_showTrayIconToggle->setEnabled(true);
            }
            m_dataService->setStartMinimized(checked);
            autoSave();
          });

  mainLayout->addWidget(launchGroup);

  // == General ==
  auto *generalGroup = new QGroupBox("General");
  auto *generalLayout = new QVBoxLayout(generalGroup);

  m_checkUpdatesToggle = new LabeledToggle("Check for updates on startup");
  m_checkUpdatesToggle->setChecked(true);
  generalLayout->addWidget(m_checkUpdatesToggle);

  connect(m_checkUpdatesToggle, &LabeledToggle::toggled, [this](bool checked) {
    m_dataService->setCheckUpdates(checked);
    autoSave();
  });

  // Theme selector
  auto *themeRow = new QHBoxLayout();
  auto *themeLabel = new QLabel("Theme:");
  UIHelpers::applyLabelRole(themeLabel);
  themeRow->addWidget(themeLabel);

  m_themeCombo = new QComboBox();
  for (auto t : ThemeManager::builtinThemes()) {
    m_themeCombo->addItem(ThemeManager::themeName(t), static_cast<int>(t));
  }
  themeRow->addWidget(m_themeCombo);
  themeRow->addStretch();
  generalLayout->addLayout(themeRow);

  connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
    auto themes = ThemeManager::builtinThemes();
    if (idx >= 0 && idx < themes.size()) {
      ThemeManager::instance().setBuiltinTheme(themes.at(idx));
      m_dataService->setSelectedTheme(idx);
      autoSave();
    }
  });

  mainLayout->addWidget(generalGroup);

  // == Maintenance & Performance ==
  auto *maintGroup = new QGroupBox("Maintenance && Performance");
  auto *maintLayout = new QVBoxLayout(maintGroup);

  m_cefCleanupToggle = new LabeledToggle("Auto-cleanup orphaned CEF processes");
  m_cefCleanupToggle->setChecked(true);
  m_cefCleanupToggle->setToolTip(
      "Automatically terminates leftover browser processes (CefHost.exe)\n"
      "that can remain running after GW2 crashes, freeing up CPU.");
  maintLayout->addWidget(m_cefCleanupToggle);

  connect(m_cefCleanupToggle, &LabeledToggle::toggled, [this](bool checked) {
    m_dataService->setCefCleanup(checked);
    CefManager::instance().setEnabled(checked);
    autoSave();
  });

  mainLayout->addWidget(maintGroup);

  // Push content up
  mainLayout->addStretch();

  // Reset to Defaults — full-width action button
  auto *resetBtn = new QPushButton("Reset to Defaults");
  UIHelpers::setThemedIcon(resetBtn, "refresh");
  resetBtn->setMinimumHeight(50);
  UIHelpers::applyCancelStyle(resetBtn);
  connect(resetBtn, &QPushButton::clicked, this, &SettingsWidget::onReset);
  mainLayout->addWidget(resetBtn);

  mainLayout->addStretch();

  // No inline setStyleSheet — all styling handled by ThemeManager global QSS
}

void SettingsWidget::loadSettings() {
  // Block signals during load to prevent auto-save cascade
  const QSignalBlocker b1(m_startMinimizedToggle);
  const QSignalBlocker b2(m_checkUpdatesToggle);
  const QSignalBlocker b3(m_showTrayIconToggle);
  const QSignalBlocker b4(m_cefCleanupToggle);
  const QSignalBlocker b5(m_themeCombo);

  m_gw2PathEdit->setText(m_dataService->gw2Path());

  bool showTray = m_dataService->showTrayIcon();
  m_showTrayIconToggle->setChecked(showTray);

  bool startMinimized = m_dataService->startMinimized();
  m_startMinimizedToggle->setChecked(startMinimized);

  m_checkUpdatesToggle->setChecked(m_dataService->checkUpdates());

  bool cefCleanup = m_dataService->cefCleanup();
  m_cefCleanupToggle->setChecked(cefCleanup);
  CefManager::instance().setEnabled(cefCleanup);

  // Apply interlocks after loading all settings
  if (startMinimized) {
    m_showTrayIconToggle->setEnabled(false);
  }
  if (!showTray) {
    m_startMinimizedToggle->setEnabled(false);
  }

  // Theme
  int savedTheme = m_dataService->selectedTheme();
  m_themeCombo->setCurrentIndex(
      qBound(0, savedTheme, m_themeCombo->count() - 1));
}

void SettingsWidget::autoSave() {
  m_dataService->syncSettings();
  emit settingsChanged();
  // Debounce the saved indicator — rapid toggles only show one flash
  m_saveDebounceTimer->start();
}

void SettingsWidget::showSavedIndicator() {
  // Stop any running animations
  if (m_savedFadeIn->state() == QAbstractAnimation::Running)
    m_savedFadeIn->stop();
  if (m_savedFadeOut->state() == QAbstractAnimation::Running)
    m_savedFadeOut->stop();
  m_savedHoldTimer->stop();

  // Fade in → hold → fade out
  m_savedFadeIn->start();
  connect(m_savedFadeIn, &QPropertyAnimation::finished, m_savedHoldTimer,
          qOverload<>(&QTimer::start), Qt::UniqueConnection);
}

void SettingsWidget::onBrowseGw2Path() {
  QString oldPath = m_gw2PathEdit->text();
  QString newPath = QFileDialog::getExistingDirectory(
      this, "Select GW2 Installation Folder", oldPath);

  if (newPath.isEmpty() || newPath == oldPath) {
    return; // User cancelled or selected same path
  }

  QDir dir(newPath);
  QString exePath;
  if (dir.exists("Gw2-64.exe")) {
    exePath = dir.filePath("Gw2-64.exe");
  } else if (dir.exists("Gw2.exe")) {
    exePath = dir.filePath("Gw2.exe");
  } else {
    // Styled warning dialog
    auto *d = UIHelpers::createStyledDialog(this, 400);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("invalidPathBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *tl = UIHelpers::createLabel(ct, "Invalid Path", 16);
    UIHelpers::applyGoldTitleRole(tl);
    tl->setAlignment(Qt::AlignCenter);
    cl->addWidget(tl);
    auto *lb = UIHelpers::createLabel(
        ct, "The selected folder doesn't contain the GW2 executable.\nPlease "
            "select the folder containing Gw2-64.exe");
    lb->setAlignment(Qt::AlignCenter);
    cl->addWidget(lb);
    ly->addWidget(ct);
    auto *ok = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(ok);
    ok->setMinimumHeight(36);
    connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    ly->addWidget(ok);
    UIHelpers::centerDialog(d);
    d->exec();
    d->deleteLater();
    return;
  }

  // === Styled "GW2 Update Required" dialog ===
  QDialog *promptDialog = new QDialog(nullptr); // Parentless for always-on-top
  promptDialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                               Qt::FramelessWindowHint);
  promptDialog->setMinimumWidth(400);
  UIHelpers::applyDialogRole(promptDialog);

  auto *promptLayout = new QVBoxLayout(promptDialog);
  promptLayout->setSpacing(20);
  promptLayout->setContentsMargins(30, 30, 30, 30);

  // Styled message container
  auto *promptContainer = UIHelpers::createMessageContainer(promptDialog);
  auto *promptLabel = UIHelpers::createLabel(
      promptContainer, "<b>GW2 Update Required</b><br><br>"
                       "When changing GW2 installation paths, the game must be "
                       "updated first.<br><br>"
                       "This will launch GW2 to check for updates. Please wait "
                       "for patching to complete, "
                       "then close the game to continue.<br><br>"
                       "Launch GW2 now to check for updates?");
  promptLabel->setAlignment(Qt::AlignCenter);
  qobject_cast<QVBoxLayout *>(promptContainer->layout())
      ->addWidget(promptLabel);
  promptLayout->addWidget(promptContainer);

  auto *yesBtn = new QPushButton("Launch GW2");
  yesBtn->setMinimumHeight(50);
  UIHelpers::applyActionStyle(yesBtn);
  connect(yesBtn, &QPushButton::clicked, promptDialog, &QDialog::accept);
  promptLayout->addWidget(yesBtn);

  auto *cancelBtn = new QPushButton("Cancel");
  cancelBtn->setMinimumHeight(40);
  UIHelpers::applyCancelStyle(cancelBtn);
  connect(cancelBtn, &QPushButton::clicked, promptDialog, &QDialog::reject);
  promptLayout->addWidget(cancelBtn);

  UIHelpers::centerDialog(promptDialog);

  if (promptDialog->exec() != QDialog::Accepted) {
    promptDialog->deleteLater();
    return; // User cancelled - no changes
  }
  promptDialog->deleteLater();

  // Launch GW2 plain (no arguments) for update check
  QProcess *updateProcess = new QProcess(this);
  updateProcess->setProgram(exePath);
  updateProcess->setWorkingDirectory(newPath);

  // === Styled "Waiting for GW2 Update" dialog ===
  QDialog *waitDialog = new QDialog(nullptr); // Parentless for always-on-top
  waitDialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                             Qt::FramelessWindowHint);
  waitDialog->setMinimumWidth(400);
  UIHelpers::applyDialogRole(waitDialog);

  auto *waitLayout = new QVBoxLayout(waitDialog);
  waitLayout->setSpacing(20);
  waitLayout->setContentsMargins(30, 30, 30, 30);

  // Styled message container
  auto *waitContainer = UIHelpers::createMessageContainer(waitDialog);
  auto *waitLabel = UIHelpers::createLabel(
      waitContainer, "<b>Waiting for GW2 Update</b><br><br>"
                     "GW2 is running for update check...<br><br>"
                     "Please wait for patching to complete, then close GW2.<br>"
                     "This dialog will close automatically when GW2 exits.");
  waitLabel->setAlignment(Qt::AlignCenter);
  qobject_cast<QVBoxLayout *>(waitContainer->layout())->addWidget(waitLabel);
  waitLayout->addWidget(waitContainer);

  UIHelpers::centerDialog(waitDialog);

  // Connect process finished to close dialog and update path
  connect(
      updateProcess,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
      [this, newPath, waitDialog, updateProcess](int exitCode,
                                                 QProcess::ExitStatus) {
        Q_UNUSED(exitCode);
        waitDialog->close();
        waitDialog->deleteLater();
        updateProcess->deleteLater();

        // Update succeeded - apply new path
        m_gw2PathEdit->setText(newPath);
        emit gw2PathChanged(newPath);

        // === Styled "Path Updated" dialog ===
        QDialog *successDialog = new QDialog(nullptr);
        successDialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                                      Qt::FramelessWindowHint);
        successDialog->setMinimumWidth(400);
        UIHelpers::applyDialogRole(successDialog);

        auto *successLayout = new QVBoxLayout(successDialog);
        successLayout->setSpacing(20);
        successLayout->setContentsMargins(30, 30, 30, 30);

        // Styled message container
        auto *successContainer =
            UIHelpers::createMessageContainer(successDialog);
        auto *successLabel = UIHelpers::createLabel(
            successContainer, "<b>Path Updated</b><br><br>"
                              "GW2 path has been updated successfully.");
        successLabel->setAlignment(Qt::AlignCenter);
        qobject_cast<QVBoxLayout *>(successContainer->layout())
            ->addWidget(successLabel);
        successLayout->addWidget(successContainer);

        auto *okBtn = new QPushButton("OK");
        okBtn->setMinimumHeight(50);
        UIHelpers::applyPrimaryStyle(okBtn);
        connect(okBtn, &QPushButton::clicked, successDialog, &QDialog::accept);
        successLayout->addWidget(okBtn);

        UIHelpers::centerDialog(successDialog);

        successDialog->exec();
        successDialog->deleteLater();
      });

  // Start process (not detached, so we can track when it finishes)
  updateProcess->start();
  if (!updateProcess->waitForStarted(5000)) {
    // Styled warning dialog
    auto *d = UIHelpers::createStyledDialog(this, 420);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *tl = UIHelpers::createLabel(ct, "Launch Failed", 16);
    UIHelpers::applyGoldTitleRole(tl);
    tl->setAlignment(Qt::AlignCenter);
    cl->addWidget(tl);
    auto *lb = UIHelpers::createLabel(
        ct, "Failed to launch GW2 for update check.\nPlease try again or run "
            "GW2 manually first.");
    lb->setAlignment(Qt::AlignCenter);
    cl->addWidget(lb);
    ly->addWidget(ct);
    auto *ok = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(ok);
    ok->setMinimumHeight(36);
    connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    ly->addWidget(ok);
    UIHelpers::centerDialog(d);
    d->exec();
    d->deleteLater();
    waitDialog->deleteLater();
    updateProcess->deleteLater();
    return;
  }

  // Show non-modal waiting dialog
  waitDialog->show();
}

void SettingsWidget::onReset() {
  // Styled confirmation dialog
  auto *d = UIHelpers::createStyledDialog(this, 380);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);
  ly->setSpacing(15);
  auto *ct = UIHelpers::createMessageContainer(bg);
  auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
  auto *tl = UIHelpers::createLabel(ct, "Reset Settings", 16);
  UIHelpers::applyGoldTitleRole(tl);
  tl->setAlignment(Qt::AlignCenter);
  cl->addWidget(tl);
  auto *lb = UIHelpers::createLabel(ct, "Reset all settings to defaults?");
  lb->setAlignment(Qt::AlignCenter);
  cl->addWidget(lb);
  ly->addWidget(ct);
  auto *btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(12);
  auto *noBtn = new QPushButton("No");
  UIHelpers::applyCancelStyle(noBtn);
  noBtn->setMinimumHeight(36);
  connect(noBtn, &QPushButton::clicked, d, &QDialog::reject);
  btnLayout->addWidget(noBtn);
  auto *yesBtn = new QPushButton("Yes");
  UIHelpers::applyPrimaryStyle(yesBtn);
  yesBtn->setMinimumHeight(36);
  connect(yesBtn, &QPushButton::clicked, d, &QDialog::accept);
  btnLayout->addWidget(yesBtn);
  ly->addLayout(btnLayout);
  UIHelpers::centerDialog(d);

  if (d->exec() == QDialog::Accepted) {
    m_showTrayIconToggle->setEnabled(true);
    m_startMinimizedToggle->setEnabled(true);
    m_showTrayIconToggle->setChecked(true);
    m_startMinimizedToggle->setChecked(false);
    m_checkUpdatesToggle->setChecked(true);
    m_cefCleanupToggle->setChecked(true);
    m_themeCombo->setCurrentIndex(0); // Classic Gold
    // Auto-save triggers handle persistence via signal connections
  }
  d->deleteLater();
}

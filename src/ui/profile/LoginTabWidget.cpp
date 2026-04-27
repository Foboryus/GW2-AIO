#include "LoginTabWidget.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "core/APIKeyManager.h"
#include "core/DataService.h"
#include "core/LocalDatManager.h"
#include "ui/ProfileEditor.h" // For AccountProfile
#include "ui/ToggleSwitch.h"  // For LabeledToggle
#include "ui/UIHelpers.h"

LoginTabWidget::LoginTabWidget(AccountProfile &profile,
                               DataService *dataService, QWidget *parent)
    : QWidget(parent), m_profile(profile), m_dataService(dataService),
      m_apiKeyManager(dataService ? dataService->apiKeyManager() : nullptr) {
  setupUI();
}

void LoginTabWidget::setupUI() {
  auto *layout = new QVBoxLayout(this);

  // Local.dat (Fast Login) - FIRST
  auto *datGroup = new QGroupBox("Local.dat (Fast Login)");
  auto *datLayout = new QVBoxLayout(datGroup);

  auto *datInfo = new QLabel(
      "GW2 stores your login in Local.dat. Save it for instant login.\n"
      "Steps: Log in normally → check 'Remember' boxes → close GW2 → Save");
  datInfo->setWordWrap(true);
  UIHelpers::applyHintRole(datInfo);
  datLayout->addWidget(datInfo);

  m_localDatLabel = new QLabel("No Local.dat saved for this profile");
  UIHelpers::applyStatusRole(m_localDatLabel);
  datLayout->addWidget(m_localDatLabel);

  auto *datBtnLayout = new QHBoxLayout();

  auto *saveDatBtn =
      new QPushButton(UIHelpers::themedIcon("save"), "Save Current Login");
  UIHelpers::applyPrimaryStyle(saveDatBtn);
  saveDatBtn->setToolTip("Save the current Local.dat for this profile");
  connect(saveDatBtn, &QPushButton::clicked, [this]() {
    if (m_profile.id.isEmpty()) {
      UIHelpers::showInfoDialog(this, "Please enter a profile name first.");
      return;
    }

    // Get the Local.dat path - GW2 stores it in %APPDATA%\Guild Wars 2
    // (Roaming)
    QString srcPath =
        QDir::homePath() + "/AppData/Roaming/Guild Wars 2/Local.dat";

    qInfo() << "Looking for Local.dat at:" << srcPath;

    if (!QFile::exists(srcPath)) {
      UIHelpers::showInfoDialog(
          this,
          QString(
              "No Local.dat found at:\n%1\n\nPlease:\n1. Launch GW2 and log "
              "in\n2. Check 'Remember Email' and 'Remember Password'\n3. Close "
              "GW2 completely\n4. Click this button again")
              .arg(srcPath));
      return;
    }

    // Check if Local.dat is a symlink from old code — can't save from that
    QFileInfo srcInfo(srcPath);
    if (srcInfo.isSymLink()) {
      UIHelpers::showInfoDialog(
          this,
          "Local.dat is a symbolic link from a previous AIO "
          "version.\n\nPlease:\n1. Launch GW2 clean (not through "
          "AIO)\n2. Log in normally\n3. Close GW2\n4. Try again",
          "Local.dat Issue");
      return;
    }

    // Check if Local.dat is corrupt (empty or too small)
    qint64 fileSize = srcInfo.size();
    qInfo() << "Local.dat size:" << fileSize << "bytes";

    if (fileSize < 1000) { // Less than 1KB = likely corrupt/empty
      UIHelpers::showInfoDialog(
          this,
          QString(
              "Your Local.dat appears corrupt or empty (only %1 bytes).\n\n"
              "This can happen after a crash or multibox session.\n\n"
              "Please:\n1. Launch GW2 and log in\n2. Check 'Remember' boxes\n"
              "3. Close GW2 cleanly via menu\n4. Try again")
              .arg(fileSize),
          "Local.dat Issue Detected");
      return;
    }

    // Use LocalDatManager to save to the profile's folder
    // (ProfileData/{profileId}/Local.dat)
    auto *ldm = m_dataService->localDatManager();
    if (!ldm) {
      UIHelpers::showErrorDialog(this, "Local.dat manager not available.");
      return;
    }

    LocalDatFile result = ldm->saveCurrentForAccount(m_profile.id);
    if (result.valid) {
      m_profile.localDatPath = result.path;
      m_localDatLabel->setText("✅ Saved: " + result.path);
      UIHelpers::applySuccessColorRole(m_localDatLabel);
      UIHelpers::showSuccessDialog(this, "Login saved successfully!");
      emit modified();
    } else {
      QString errorMsg = "Failed to save Local.dat.\n\n";
      if (!QFile::exists(srcPath)) {
        errorMsg += "Source file does not exist.";
      } else {
        QFile srcFile(srcPath);
        if (!srcFile.open(QIODevice::ReadOnly)) {
          errorMsg += "Cannot open source file - GW2 may be running.\n";
          errorMsg += "Please close GW2 completely and try again.";
        } else {
          srcFile.close();
          errorMsg += "Unknown error during file copy.";
        }
      }
      UIHelpers::showErrorDialog(this, errorMsg);
    }
  });
  datBtnLayout->addWidget(saveDatBtn);

  auto *browseDatBtn =
      new QPushButton(UIHelpers::themedIcon("folder"), "Browse...");
  UIHelpers::applyNeutralStyle(browseDatBtn);
  connect(browseDatBtn, &QPushButton::clicked, [this]() {
    QString path = QFileDialog::getOpenFileName(this, "Select Local.dat",
                                                QString(), "Local.dat (*.dat)");
    if (!path.isEmpty()) {
      m_profile.localDatPath = path;
      m_localDatLabel->setText("✅ " + QFileInfo(path).fileName());
      UIHelpers::applySuccessColorRole(m_localDatLabel);
      emit modified();
    }
  });
  datBtnLayout->addWidget(browseDatBtn);
  datBtnLayout->addStretch();

  datLayout->addLayout(datBtnLayout);
  layout->addWidget(datGroup);

  // Auto-login toggle
  m_autoLoginToggle =
      new LabeledToggle("Use saved Local.dat for automatic login");
  m_autoLoginToggle->setToolTip(
      "When enabled, the saved Local.dat will be applied before launch");
  layout->addWidget(m_autoLoginToggle);
  connect(m_autoLoginToggle, &LabeledToggle::toggled, this,
          &LoginTabWidget::modified);

  // Custom GW2 Installation Path section
  auto *pathGroup = new QGroupBox("Custom GW2 Installation");
  auto *pathLayout = new QVBoxLayout(pathGroup);

  m_customPathToggle = new LabeledToggle("Use custom GW2 installation path");
  m_customPathToggle->setToolTip(
      "Enable to use a different GW2 installation for this profile");
  pathLayout->addWidget(m_customPathToggle);
  connect(m_customPathToggle, &LabeledToggle::toggled, this,
          &LoginTabWidget::modified);

  auto *pathNote = new QLabel("Use this if you have multiple GW2 installations "
                              "(Steam, Epic, standalone)");
  UIHelpers::applyHintRole(pathNote);
  pathNote->setStyleSheet(
      QString("font-size: %1px; margin-left: 52px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  pathLayout->addWidget(pathNote);

  m_customPathLabel = new QLabel("No custom path set - using global GW2 path");
  UIHelpers::applySecondaryRole(m_customPathLabel);
  m_customPathLabel->setStyleSheet(
      QString("padding: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.paddingSmall));
  pathLayout->addWidget(m_customPathLabel);

  auto *pathBtnLayout = new QHBoxLayout();
  auto *browsePathBtn = new QPushButton(UIHelpers::themedIcon("folder"),
                                        "Browse for Gw2-64.exe...");
  UIHelpers::applyNeutralStyle(browsePathBtn);
  browsePathBtn->setEnabled(false); // Disabled until toggle is ON
  connect(browsePathBtn, &QPushButton::clicked, [this]() {
    QString path = QFileDialog::getOpenFileName(
        this, "Select Gw2-64.exe", QString(), "GW2 Executable (Gw2-64.exe)");
    if (!path.isEmpty()) {
      m_profile.customGw2Path = path;
      m_customPathLabel->setText("✅ " + path);
      UIHelpers::applySuccessColorRole(m_customPathLabel);
      m_customPathLabel->setStyleSheet(
          QString("padding: %1px;")
              .arg(ThemeManager::instance().activeTheme().layout.paddingSmall));
      emit modified();
    }
  });
  pathBtnLayout->addWidget(browsePathBtn);
  pathBtnLayout->addStretch();
  pathLayout->addLayout(pathBtnLayout);

  // Connect toggle to enable/disable browse button
  connect(m_customPathToggle, &LabeledToggle::toggled,
          [browsePathBtn, this](bool checked) {
            browsePathBtn->setEnabled(checked);
            if (!checked) {
              m_customPathLabel->setText(
                  "No custom path set - using global GW2 path");
              UIHelpers::applySecondaryRole(m_customPathLabel);
              m_customPathLabel->setStyleSheet(
                  QString("padding: %1px;")
                      .arg(ThemeManager::instance()
                               .activeTheme()
                               .layout.paddingSmall));
            }
          });

  layout->addWidget(pathGroup);

  // ===== GW2 Account API Section =====
  auto *apiGroup = new QGroupBox("GW2 Account API");
  auto *apiLayout = new QVBoxLayout(apiGroup);

  auto *apiHint = new QLabel(
      "Enter your API key from account.arena.net. "
      "AIO only reads data — it cannot modify your account.");
  apiHint->setWordWrap(true);
  UIHelpers::applyHintRole(apiHint);
  apiLayout->addWidget(apiHint);

  // Key input row
  auto *keyInputLayout = new QHBoxLayout();

  m_apiKeyInput = new QLineEdit();
  m_apiKeyInput->setPlaceholderText("XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX...");
  m_apiKeyInput->setEchoMode(QLineEdit::Password);
  m_apiKeyInput->setMinimumHeight(32);
  keyInputLayout->addWidget(m_apiKeyInput, 1);

  // Show/hide toggle
  m_apiKeyShowBtn = new QPushButton(UIHelpers::themedIcon("eye"), "");
  m_apiKeyShowBtn->setFixedSize(32, 32);
  m_apiKeyShowBtn->setToolTip("Show/hide API key");
  m_apiKeyShowBtn->setCheckable(true);
  UIHelpers::applyNeutralStyle(m_apiKeyShowBtn);
  connect(m_apiKeyShowBtn, &QPushButton::toggled, [this](bool checked) {
    m_apiKeyInput->setEchoMode(checked ? QLineEdit::Normal
                                       : QLineEdit::Password);
    UIHelpers::setThemedIcon(m_apiKeyShowBtn, checked ? "eye-off" : "eye");
  });
  keyInputLayout->addWidget(m_apiKeyShowBtn);
  apiLayout->addLayout(keyInputLayout);

  // Status label
  m_apiKeyStatus = new QLabel("No API key configured");
  UIHelpers::applyStatusRole(m_apiKeyStatus);
  apiLayout->addWidget(m_apiKeyStatus);

  // Button row
  auto *apiBtnLayout = new QHBoxLayout();

  m_validateBtn = new QPushButton(UIHelpers::themedIcon("check-circle"),
                                  "Validate");
  UIHelpers::applyPrimaryStyle(m_validateBtn);
  m_validateBtn->setToolTip("Test the API key and show permissions");
  connect(m_validateBtn, &QPushButton::clicked, [this]() {
    if (!m_apiKeyManager) return;
    QString key = m_apiKeyInput->text().trimmed();
    if (key.isEmpty()) {
      UIHelpers::showInfoDialog(this, "Please enter an API key.");
      return;
    }
    m_apiKeyStatus->setText("Validating...");
    UIHelpers::applyStatusRole(m_apiKeyStatus);
    m_validateBtn->setEnabled(false);
    m_apiKeyManager->validateKey(m_profile.id, key);
  });
  apiBtnLayout->addWidget(m_validateBtn);

  auto *generateBtn = new QPushButton(UIHelpers::themedIcon("link"),
                                      "Generate Key");
  UIHelpers::applyNeutralStyle(generateBtn);
  generateBtn->setToolTip("Open ArenaNet API key creation page");
  connect(generateBtn, &QPushButton::clicked, []() {
    QDesktopServices::openUrl(
        QUrl("https://account.arena.net/applications/create"));
  });
  apiBtnLayout->addWidget(generateBtn);

  m_removeKeyBtn = new QPushButton(UIHelpers::themedIcon("trash"),
                                   "Remove Key");
  UIHelpers::applyCancelStyle(m_removeKeyBtn);
  m_removeKeyBtn->setToolTip("Delete the stored API key");
  m_removeKeyBtn->setVisible(false); // Only show if key exists
  connect(m_removeKeyBtn, &QPushButton::clicked, [this]() {
    if (!m_apiKeyManager) return;
    m_apiKeyManager->removeKey(m_profile.id);
    m_apiKeyInput->clear();
    m_apiKeyStatus->setText("API key removed");
    UIHelpers::applyStatusRole(m_apiKeyStatus);
    m_removeKeyBtn->setVisible(false);
    emit modified();
  });
  apiBtnLayout->addWidget(m_removeKeyBtn);
  apiBtnLayout->addStretch();

  apiLayout->addLayout(apiBtnLayout);
  layout->addWidget(apiGroup);

  // Connect validation signals
  if (m_apiKeyManager) {
    connect(m_apiKeyManager, &APIKeyManager::keyValidated, this,
            [this](const QString &profileId,
                   const APIKeyManager::TokenInfo &tokenInfo,
                   const QString &accountName) {
              if (profileId != m_profile.id) return;
              m_validateBtn->setEnabled(true);

              // Store the key on successful validation
              QString key = m_apiKeyInput->text().trimmed();
              m_apiKeyManager->storeKey(m_profile.id, key);

              // Build status text
              QString status = "✅ Valid";
              if (!accountName.isEmpty()) {
                status += " — " + accountName;
              }
              if (!tokenInfo.permissions.isEmpty()) {
                status += "\nPermissions: " +
                          tokenInfo.permissions.join(", ");
              }
              m_apiKeyStatus->setText(status);
              UIHelpers::applySuccessColorRole(m_apiKeyStatus);
              m_removeKeyBtn->setVisible(true);
              m_profile.hasApiKey = true;
              emit modified();
            });

    connect(m_apiKeyManager, &APIKeyManager::keyValidationFailed, this,
            [this](const QString &profileId, const QString &error) {
              if (profileId != m_profile.id) return;
              m_validateBtn->setEnabled(true);
              m_apiKeyStatus->setText("❌ Validation failed: " + error);
              UIHelpers::applyErrorColorRole(m_apiKeyStatus);
            });
  }

  layout->addStretch();
}

void LoginTabWidget::load() {
  // Local.dat status
  if (!m_profile.localDatPath.isEmpty()) {
    m_localDatLabel->setText("✅ Local.dat: " + m_profile.localDatPath);
    UIHelpers::applySuccessColorRole(m_localDatLabel);
  }

  m_autoLoginToggle->blockSignals(true);
  m_autoLoginToggle->setChecked(m_profile.autoLogin);
  m_autoLoginToggle->blockSignals(false);

  // Custom GW2 path
  m_customPathToggle->blockSignals(true);
  m_customPathToggle->setChecked(m_profile.useCustomGw2Path);
  m_customPathToggle->blockSignals(false);

  if (!m_profile.customGw2Path.isEmpty() && m_profile.useCustomGw2Path) {
    m_customPathLabel->setText("✅ " + m_profile.customGw2Path);
    m_customPathLabel->setStyleSheet(
        QString("padding: %1px;")
            .arg(ThemeManager::instance().activeTheme().layout.paddingSmall));
  }

  // API key status
  if (m_apiKeyManager && m_apiKeyManager->hasKey(m_profile.id)) {
    m_apiKeyInput->setPlaceholderText("●●●●●●●● (key stored securely)");
    m_apiKeyStatus->setText("✅ API key is stored in Credential Manager");
    UIHelpers::applySuccessColorRole(m_apiKeyStatus);
    m_removeKeyBtn->setVisible(true);
    m_profile.hasApiKey = true;
  }
}

void LoginTabWidget::save() {
  m_profile.autoLogin = m_autoLoginToggle->isChecked();
  m_profile.useCustomGw2Path = m_customPathToggle->isChecked();
  // customGw2Path and localDatPath are set directly in button handlers
}

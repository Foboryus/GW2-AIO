#include "NetworkTabWidget.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include "core/ServerManager.h"
#include "ui/ProfileEditor.h" // For AccountProfile, NetworkMode
#include "ui/ToggleSwitch.h"  // For LabeledToggle
#include "ui/UIHelpers.h"

NetworkTabWidget::NetworkTabWidget(AccountProfile &profile,
                                   ServerManager *serverManager,
                                   QWidget *parent)
    : QWidget(parent), m_profile(profile), m_serverManager(serverManager) {
  setupUI();
}

void NetworkTabWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  // Info section
  auto *networkInfo = new QLabel(
      "Network Settings\n\n"
      "Configure how this profile connects to GW2 servers.\n"
      "Use 'Global' to follow the Global Network tab, or set a custom server.");
  networkInfo->setWordWrap(true);
  UIHelpers::applySecondaryRole(networkInfo);
  networkInfo->setStyleSheet(
      QString("padding: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.paddingLarge));
  mainLayout->addWidget(networkInfo);

  // Warning about server IPs
  auto *networkWarning = new QLabel(
      "⚠ Server IPs change frequently (ArenaNet uses round-robin DNS). "
      "Only use custom servers if you're experiencing connection issues. "
      "For normal play, 'Use Default' is recommended.");
  networkWarning->setWordWrap(true);
  UIHelpers::applyWarningColorRole(networkWarning);
  networkWarning->setStyleSheet(
      QString("font-size: %1px; padding: %2px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint)
          .arg(ThemeManager::instance().activeTheme().layout.paddingNormal));
  mainLayout->addWidget(networkWarning);

  // Network mode group with 3 interlocked toggles
  auto *networkModeGroup = new QGroupBox("Network Mode");
  auto *networkModeLayout = new QVBoxLayout(networkModeGroup);

  // Default toggle
  m_defaultToggle = new LabeledToggle("Use Default (ArenaNet)");
  m_defaultToggle->setToolTip(
      "No custom server - launches like the unmodified game");
  networkModeLayout->addWidget(m_defaultToggle);

  auto *defaultNote = new QLabel("Launches GW2 without any network arguments");
  UIHelpers::applyHintRole(defaultNote);
  defaultNote->setStyleSheet(
      QString("font-size: %1px; margin-left: 52px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  networkModeLayout->addWidget(defaultNote);

  // Global toggle
  m_globalToggle = new LabeledToggle("Use Global Setting");
  m_globalToggle->setToolTip(
      "Follows whatever is configured in the Global Network tab");
  networkModeLayout->addWidget(m_globalToggle);

  auto *globalNote =
      new QLabel("Server changes in Global Network tab apply to this profile");
  UIHelpers::applyHintRole(globalNote);
  globalNote->setStyleSheet(
      QString("font-size: %1px; margin-left: 52px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  networkModeLayout->addWidget(globalNote);

  // Show current global args if ServerManager is available
  m_globalArgsLabel = new QLabel();
  if (m_serverManager) {
    QStringList args;
    QString authArg = m_serverManager->authServerArg();
    QString assetArg = m_serverManager->assetServerArg();
    QString portArg = m_serverManager->clientPortArg();
    if (!authArg.isEmpty())
      args << authArg;
    if (!assetArg.isEmpty())
      args << assetArg;
    if (!portArg.isEmpty())
      args << portArg;

    if (args.isEmpty()) {
      m_globalArgsLabel->setText("Current: Using ArenaNet defaults");
      UIHelpers::applySuccessColorRole(m_globalArgsLabel);
      m_globalArgsLabel->setStyleSheet(
          "font-size: 11px; margin-left: 52px; font-family: monospace;");
    } else {
      m_globalArgsLabel->setText("Current: " + args.join("  "));
      UIHelpers::applyGoldColorRole(m_globalArgsLabel);
      m_globalArgsLabel->setStyleSheet(
          "font-size: 11px; margin-left: 52px; font-family: monospace;");
    }
  } else {
    m_globalArgsLabel->setText("Current: (Configure in Global Network tab)");
    UIHelpers::applySecondaryRole(m_globalArgsLabel);
    m_globalArgsLabel->setStyleSheet(
        QString("font-size: %1px; margin-left: 52px;")
            .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  }
  networkModeLayout->addWidget(m_globalArgsLabel);

  // Custom toggle
  m_customToggle = new LabeledToggle("Custom Server");
  m_customToggle->setToolTip("Use a specific server for this profile only");
  networkModeLayout->addWidget(m_customToggle);

  auto *customNote = new QLabel("Profile-specific server override");
  UIHelpers::applyHintRole(customNote);
  customNote->setStyleSheet(
      QString("font-size: %1px; margin-left: 52px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  networkModeLayout->addWidget(customNote);

  mainLayout->addWidget(networkModeGroup);

  // Server dropdown (only enabled when Custom is selected)
  m_serverGroup = new QGroupBox("Custom Server");
  auto *serverLayout = new QVBoxLayout(m_serverGroup);

  m_serverCombo = new QComboBox();
  m_serverCombo->setEditable(true); // Allow manual entry
  // comboBoxStyle removed — handled by global QSS
  m_serverCombo->addItem("-- Select or enter server --", "");
  m_serverCombo->addItem(
      "Auth Portal NA (cligate-prod-live-na.ncplatform.net:443)",
      "cligate-prod-live-na.ncplatform.net:443");
  m_serverCombo->addItem(
      "Auth Portal EU (cligate-prod-live-eu.ncplatform.net:443)",
      "cligate-prod-live-eu.ncplatform.net:443");
  m_serverCombo->addItem("Port 80 (firewall bypass)",
                         "cligate-prod-live-na.ncplatform.net:80");
  m_serverCombo->addItem("Port 443 (HTTPS)",
                         "cligate-prod-live-na.ncplatform.net:443");
  serverLayout->addWidget(m_serverCombo);

  auto *serverNote = new QLabel(
      "Enter custom server as: hostname:port (e.g., 64.25.38.54:6112)");
  UIHelpers::applyHintRole(serverNote);
  serverNote->setStyleSheet(
      QString("font-size: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  serverLayout->addWidget(serverNote);

  mainLayout->addWidget(m_serverGroup);

  // Interlock the toggles using explicit handlers
  // When one is clicked, turn it ON and turn others OFF
  connect(m_defaultToggle, &LabeledToggle::toggled, [this](bool checked) {
    if (checked) {
      m_globalToggle->blockSignals(true);
      m_customToggle->blockSignals(true);
      m_globalToggle->setChecked(false);
      m_customToggle->setChecked(false);
      m_globalToggle->blockSignals(false);
      m_customToggle->blockSignals(false);
      m_serverGroup->setEnabled(false);
      emit modified();
    } else {
      // Prevent turning off - at least one must be on
      m_defaultToggle->blockSignals(true);
      m_defaultToggle->setChecked(true);
      m_defaultToggle->blockSignals(false);
    }
  });

  connect(m_globalToggle, &LabeledToggle::toggled, [this](bool checked) {
    if (checked) {
      m_defaultToggle->blockSignals(true);
      m_customToggle->blockSignals(true);
      m_defaultToggle->setChecked(false);
      m_customToggle->setChecked(false);
      m_defaultToggle->blockSignals(false);
      m_customToggle->blockSignals(false);
      m_serverGroup->setEnabled(false);
      emit modified();
    } else {
      // Prevent turning off - at least one must be on
      m_globalToggle->blockSignals(true);
      m_globalToggle->setChecked(true);
      m_globalToggle->blockSignals(false);
    }
  });

  connect(m_customToggle, &LabeledToggle::toggled, [this](bool checked) {
    if (checked) {
      m_defaultToggle->blockSignals(true);
      m_globalToggle->blockSignals(true);
      m_defaultToggle->setChecked(false);
      m_globalToggle->setChecked(false);
      m_defaultToggle->blockSignals(false);
      m_globalToggle->blockSignals(false);
      m_serverGroup->setEnabled(true);
      emit modified();
    } else {
      // Prevent turning off - at least one must be on
      m_customToggle->blockSignals(true);
      m_customToggle->setChecked(true);
      m_customToggle->blockSignals(false);
    }
  });

  // Server combo changes emit modified
  connect(m_serverCombo, &QComboBox::currentIndexChanged, this,
          [this]() { emit modified(); });
  connect(m_serverCombo, &QComboBox::currentTextChanged, this,
          [this]() { emit modified(); });

  // Set defaults and initial state
  m_defaultToggle->setChecked(true); // Default for new profiles
  m_serverGroup->setEnabled(false);  // Start disabled

  mainLayout->addStretch();
}

void NetworkTabWidget::load() {
  // Block signals during load
  m_defaultToggle->blockSignals(true);
  m_globalToggle->blockSignals(true);
  m_customToggle->blockSignals(true);

  m_defaultToggle->setChecked(m_profile.networkMode == NetworkMode::UseDefault);
  m_globalToggle->setChecked(m_profile.networkMode == NetworkMode::UseGlobal);
  m_customToggle->setChecked(m_profile.networkMode == NetworkMode::Custom);

  m_defaultToggle->blockSignals(false);
  m_globalToggle->blockSignals(false);
  m_customToggle->blockSignals(false);

  // Update server group enabled state based on mode
  m_serverGroup->setEnabled(m_profile.networkMode == NetworkMode::Custom);

  // Set custom server in combo
  if (!m_profile.customNetworkServer.isEmpty()) {
    int idx = m_serverCombo->findData(m_profile.customNetworkServer);
    if (idx >= 0) {
      m_serverCombo->setCurrentIndex(idx);
    } else {
      m_serverCombo->setCurrentText(m_profile.customNetworkServer);
    }
  }
}

void NetworkTabWidget::save() {
  // Determine network mode based on which toggle is checked
  if (m_defaultToggle->isChecked()) {
    m_profile.networkMode = NetworkMode::UseDefault;
  } else if (m_globalToggle->isChecked()) {
    m_profile.networkMode = NetworkMode::UseGlobal;
  } else if (m_customToggle->isChecked()) {
    m_profile.networkMode = NetworkMode::Custom;
  }

  // Save custom server (from combo selection or manual entry)
  if (m_profile.networkMode == NetworkMode::Custom) {
    QVariant data = m_serverCombo->currentData();
    if (data.isValid() && !data.toString().isEmpty()) {
      m_profile.customNetworkServer = data.toString();
    } else {
      m_profile.customNetworkServer = m_serverCombo->currentText();
    }
  } else {
    m_profile.customNetworkServer.clear();
  }
}

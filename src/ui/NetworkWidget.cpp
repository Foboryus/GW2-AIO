/**
 * @file NetworkWidget.cpp
 * @brief Implementation of NetworkWidget class
 *
 * This file contains the implementation of network UI functionality:
 * - Server table display (auth/asset servers with ping)
 * - Server selection for launch arguments
 * - Port configuration
 * - DNS refresh/ping functionality
 *
 * DO NOT add:
 * - Actual network ping logic (belongs in ServerManager)
 * - Profile-specific network settings (belongs in ProfileEditor)
 * - Launch logic (belongs in LaunchManager)
 */

#include "NetworkWidget.h"
#include "UIHelpers.h"
#include "core/ThemeManager.h"
#include <QRegularExpression>

NetworkWidget::NetworkWidget(QWidget *parent)
    : QWidget(parent), m_serverManager(new ServerManager(this)) {
  setupUI();

  connect(m_serverManager, &ServerManager::serversUpdated, this,
          &NetworkWidget::onServersUpdated);
  connect(m_serverManager, &ServerManager::refreshStarted, [this]() {
    m_refreshBtn->setEnabled(false);
    m_refreshBtn->setText("Checking...");
  });
  connect(m_serverManager, &ServerManager::refreshFinished, [this]() {
    m_refreshBtn->setEnabled(true);
    m_refreshBtn->setText("Update Servers");
    UIHelpers::setThemedIcon(m_refreshBtn, "refresh");
  });
}

void NetworkWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Auto-refresh on first open
  if (!m_hasLoaded) {
    m_hasLoaded = true;
    // Small delay to let UI render first
    QTimer::singleShot(100, this, &NetworkWidget::onRefresh);
  }
}

void NetworkWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);

  // Header
  auto *header =
      UIHelpers::createPageHeader(this, "Network Status", ":/icons/wifi.svg");
  auto *headerLayout = qobject_cast<QHBoxLayout *>(header->layout());

  auto *resetBtn = new QPushButton("Reset to Default");
  UIHelpers::setThemedIcon(resetBtn, "refresh");
  UIHelpers::applyCancelStyle(resetBtn);
  connect(resetBtn, &QPushButton::clicked, [this]() {
    m_authTable->clearSelection();
    m_assetTable->clearSelection();
    m_serverManager->setSelectedAuthServer(-1);
    m_serverManager->setSelectedAssetServer(-1);
    m_portCombo->setCurrentIndex(0);
    m_selectedAuthLabel->setText("Selected: Default");
    UIHelpers::applySuccessColorRole(m_selectedAuthLabel);
    m_selectedAssetLabel->setText("Selected: Default");
    UIHelpers::applySuccessColorRole(m_selectedAssetLabel);
    updateLaunchArgsInfo();
  });
  headerLayout->addWidget(resetBtn);

  m_refreshBtn = new QPushButton("Update Servers");
  UIHelpers::setThemedIcon(m_refreshBtn, "refresh");
  UIHelpers::applyNeutralStyle(m_refreshBtn);
  connect(m_refreshBtn, &QPushButton::clicked, this, &NetworkWidget::onRefresh);
  headerLayout->addWidget(m_refreshBtn);

  // Apply to All Profiles button
  auto *applyAllBtn = new QPushButton("Apply to All Profiles");
  UIHelpers::setThemedIcon(applyAllBtn, "layers");
  UIHelpers::applyPrimaryStyle(applyAllBtn);
  applyAllBtn->setToolTip(
      "Set all profiles to use these global network settings");
  connect(applyAllBtn, &QPushButton::clicked, [this]() {
    // Styled confirmation dialog
    auto *d = new QDialog(this);
    d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    d->setAttribute(Qt::WA_TranslucentBackground);
    d->setMinimumWidth(450);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("applyAllBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    ly->setSpacing(15);
    auto *tl = new QLabel("Apply to All Profiles");
    UIHelpers::applyGoldTitleRole(tl);
    tl->setAlignment(Qt::AlignCenter);
    ly->addWidget(tl);
    auto *lb =
        new QLabel("This will:\n• Set ALL profiles to 'Use Global Setting' "
                   "mode\n• Remove any network args (-authsrv, -portal, "
                   "-clientport) from Arguments tabs\n\nAll profiles will then "
                   "use the settings configured on this page.\n\nContinue?");
    UIHelpers::applyPopupLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
    lb->setWordWrap(true);
    ly->addWidget(lb);
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);
    auto *noBtn = new QPushButton("No");
    noBtn->setMinimumHeight(36);
    UIHelpers::applyCancelStyle(noBtn);
    connect(noBtn, &QPushButton::clicked, d, &QDialog::reject);
    btnLayout->addWidget(noBtn);
    auto *yesBtn = new QPushButton("Yes");
    yesBtn->setMinimumHeight(36);
    UIHelpers::applyPrimaryStyle(yesBtn);
    connect(yesBtn, &QPushButton::clicked, d, &QDialog::accept);
    btnLayout->addWidget(yesBtn);
    ly->addLayout(btnLayout);

    if (d->exec() == QDialog::Accepted) {
      emit applyToAllProfiles();
      // Styled success dialog
      auto *s = new QDialog(this);
      s->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
      s->setAttribute(Qt::WA_TranslucentBackground);
      s->setMinimumWidth(420);
      auto *sol = new QVBoxLayout(s);
      sol->setContentsMargins(0, 0, 0, 0);
      auto *sbg = new QWidget();
      sbg->setObjectName("appliedBg");
      UIHelpers::applyPopupBackgroundRole(sbg);
      sol->addWidget(sbg);
      auto *sly = new QVBoxLayout(sbg);
      sly->setContentsMargins(20, 20, 20, 20);
      auto *slb = new QLabel(
          "All profiles are now using global network settings.\n\nTo revert a "
          "profile to ArenaNet defaults:\nEdit Profile → Network tab → Select "
          "'Use Default (ArenaNet)'");
      UIHelpers::applySuccessColorRole(slb);
      slb->setAlignment(Qt::AlignCenter);
      slb->setWordWrap(true);
      sly->addWidget(slb);
      auto *sok = new QPushButton("OK");
      sok->setMinimumHeight(36);
      UIHelpers::applyPrimaryStyle(sok);
      connect(sok, &QPushButton::clicked, s, &QDialog::accept);
      sly->addWidget(sok);
      s->exec();
      s->deleteLater();
    }
    d->deleteLater();
  });
  headerLayout->addWidget(applyAllBtn);

  mainLayout->addWidget(header);

  // === Launch args info box - AT TOP ===
  auto *argsGroup = new QGroupBox("Launch Arguments (added to game command)");
  auto *argsLayout = new QVBoxLayout(argsGroup);
  m_launchArgsLabel =
      new QLabel("No custom servers selected - using GW2 defaults");
  UIHelpers::applySuccessColorRole(m_launchArgsLabel);
  m_launchArgsLabel->setStyleSheet(
      "font-family: 'Consolas', monospace; padding: 8px;");
  m_launchArgsLabel->setWordWrap(true);
  argsLayout->addWidget(m_launchArgsLabel);

  auto *argsNote = new QLabel("Select a server below to force GW2 to connect "
                              "to it. Double-click to deselect.");
  UIHelpers::applyHintRole(argsNote);
  argsLayout->addWidget(argsNote);

  auto *warningNote =
      new QLabel("⚠ Note: Server IPs change frequently. ArenaNet uses "
                 "round-robin DNS, so an IP "
                 "that worked before may become unavailable. Only override "
                 "servers if you're experiencing "
                 "connection issues (e.g., firewall blocking ports). For "
                 "normal play, use 'Reset to Default'.");
  UIHelpers::applyWarningColorRole(warningNote);
  warningNote->setStyleSheet(
      QString("font-size: %1px; padding: %2px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint)
          .arg(ThemeManager::instance().activeTheme().layout.paddingSmall));
  warningNote->setWordWrap(true);
  argsLayout->addWidget(warningNote);
  mainLayout->addWidget(argsGroup);

  // Client port
  auto *portGroup = new QGroupBox("Client Port");
  auto *portLayout = new QHBoxLayout(portGroup);

  portLayout->addWidget(new QLabel("Use port:"));
  m_portCombo = new QComboBox();
  m_portCombo->addItem("Default (6112)", 0);
  m_portCombo->addItem("443 (HTTPS - bypasses firewalls)", 443);
  m_portCombo->addItem("80 (HTTP - bypasses firewalls)", 80);
  m_portCombo->addItem("Custom...", -1);
  m_portCombo->setMinimumWidth(200);
  portLayout->addWidget(m_portCombo);

  // Custom port spinbox (hidden by default)
  auto *customPortSpin = new QSpinBox();
  customPortSpin->setRange(1, 65535);
  customPortSpin->setValue(6112);
  customPortSpin->setVisible(false);
  customPortSpin->setMinimumWidth(80);
  portLayout->addWidget(customPortSpin);

  connect(m_portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this, customPortSpin](int index) {
            int portValue = m_portCombo->currentData().toInt();
            if (portValue == -1) {
              customPortSpin->setVisible(true);
              m_serverManager->setClientPort(customPortSpin->value());
            } else {
              customPortSpin->setVisible(false);
              m_serverManager->setClientPort(portValue);
            }
            updateLaunchArgsInfo();
          });

  connect(customPortSpin, QOverload<int>::of(&QSpinBox::valueChanged),
          [this](int value) {
            if (m_portCombo->currentData().toInt() == -1) {
              m_serverManager->setClientPort(value);
              updateLaunchArgsInfo();
            }
          });

  portLayout->addStretch();

  auto *portNote =
      new QLabel("Use 443 or 80 if default port is blocked by firewall");
  UIHelpers::applyHintRole(portNote);
  portLayout->addWidget(portNote);

  mainLayout->addWidget(portGroup);

  // Authentication Servers
  auto *authGroup = new QGroupBox("Authentication Servers");
  auto *authLayout = new QVBoxLayout(authGroup);

  auto *authHeaderLayout = new QHBoxLayout();
  m_authCountLabel = new QLabel("0 servers found");
  UIHelpers::applyHintRole(m_authCountLabel);
  authHeaderLayout->addWidget(m_authCountLabel);
  authHeaderLayout->addStretch();

  m_selectedAuthLabel = new QLabel("Selected: Default");
  UIHelpers::applySuccessColorRole(m_selectedAuthLabel);
  authHeaderLayout->addWidget(m_selectedAuthLabel);

  auto *addAuthBtn = new QPushButton("Add Custom");
  UIHelpers::setThemedIcon(addAuthBtn, "plus");
  UIHelpers::applyNeutralStyle(addAuthBtn);
  addAuthBtn->setMaximumWidth(120);
  connect(addAuthBtn, &QPushButton::clicked, this,
          &NetworkWidget::onAddAuthServer);
  authHeaderLayout->addWidget(addAuthBtn);
  authLayout->addLayout(authHeaderLayout);

  m_authTable = new QTableWidget();
  m_authTable->setColumnCount(5);
  m_authTable->setHorizontalHeaderLabels(
      {"Status", "IP Address", "Port", "Type", "Ping (ms)"});
  m_authTable->horizontalHeader()->setStretchLastSection(true);
  m_authTable->horizontalHeader()->setSectionResizeMode(1,
                                                        QHeaderView::Stretch);
  m_authTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_authTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_authTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_authTable->setMinimumHeight(100);
  m_authTable->setMaximumHeight(150);
  connect(m_authTable, &QTableWidget::itemSelectionChanged, this,
          &NetworkWidget::onAuthSelectionChanged);
  connect(m_authTable, &QTableWidget::itemDoubleClicked,
          [this](QTableWidgetItem *) { m_authTable->clearSelection(); });
  authLayout->addWidget(m_authTable);

  mainLayout->addWidget(authGroup);

  // Asset Servers
  auto *assetGroup = new QGroupBox("Asset/CDN Servers");
  auto *assetLayout = new QVBoxLayout(assetGroup);

  auto *assetHeaderLayout = new QHBoxLayout();
  m_assetCountLabel = new QLabel("0 servers found");
  UIHelpers::applyHintRole(m_assetCountLabel);
  assetHeaderLayout->addWidget(m_assetCountLabel);
  assetHeaderLayout->addStretch();

  m_selectedAssetLabel = new QLabel("Selected: Default");
  UIHelpers::applySuccessColorRole(m_selectedAssetLabel);
  assetHeaderLayout->addWidget(m_selectedAssetLabel);

  auto *addAssetBtn = new QPushButton("Add Custom");
  UIHelpers::setThemedIcon(addAssetBtn, "plus");
  UIHelpers::applyNeutralStyle(addAssetBtn);
  addAssetBtn->setMaximumWidth(120);
  connect(addAssetBtn, &QPushButton::clicked, this,
          &NetworkWidget::onAddAssetServer);
  assetHeaderLayout->addWidget(addAssetBtn);
  assetLayout->addLayout(assetHeaderLayout);

  m_assetTable = new QTableWidget();
  m_assetTable->setColumnCount(5);
  m_assetTable->setHorizontalHeaderLabels(
      {"Status", "IP Address", "Port", "Type", "Ping (ms)"});
  m_assetTable->horizontalHeader()->setStretchLastSection(true);
  m_assetTable->horizontalHeader()->setSectionResizeMode(1,
                                                         QHeaderView::Stretch);
  m_assetTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_assetTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_assetTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_assetTable->setMinimumHeight(100);
  m_assetTable->setMaximumHeight(150);
  connect(m_assetTable, &QTableWidget::itemSelectionChanged, this,
          &NetworkWidget::onAssetSelectionChanged);
  connect(m_assetTable, &QTableWidget::itemDoubleClicked,
          [this](QTableWidgetItem *) { m_assetTable->clearSelection(); });
  assetLayout->addWidget(m_assetTable);

  mainLayout->addWidget(assetGroup);

  mainLayout->addStretch();

  // Styles come from global QSS template (ThemeManager) — DO NOT use
  // setStyleSheet here, it blocks the app-level theme cascade.
}

void NetworkWidget::onRefresh() { m_serverManager->refreshServers(); }

void NetworkWidget::onServersUpdated() {
  updateAuthTable();
  updateAssetTable();
  updateLaunchArgsInfo();
}

void NetworkWidget::updateAuthTable() {
  auto servers = m_serverManager->authServers();

  // Sort by ping (lowest first, but put timeouts and unknowns at end)
  std::sort(servers.begin(), servers.end(),
            [](const GW2Server &a, const GW2Server &b) {
              int pingA = (a.ping < 0 || a.ping >= 9999) ? 99999 : a.ping;
              int pingB = (b.ping < 0 || b.ping >= 9999) ? 99999 : b.ping;
              return pingA < pingB;
            });

  m_authTable->setRowCount(servers.size());
  m_authCountLabel->setText(QString("%1 servers found").arg(servers.size()));

  for (int i = 0; i < servers.size(); i++) {
    const auto &s = servers[i];

    auto *statusItem = new QTableWidgetItem();
    statusItem->setIcon(QIcon(statusIcon(s.ping)));
    m_authTable->setItem(i, 0, statusItem);
    m_authTable->setItem(i, 1, new QTableWidgetItem(s.ip));
    m_authTable->setItem(i, 2, new QTableWidgetItem(QString::number(s.port)));
    m_authTable->setItem(i, 3, new QTableWidgetItem(s.type));
    m_authTable->setItem(i, 4, new QTableWidgetItem(pingToString(s.ping)));

    m_authTable->item(i, 0)->setData(Qt::UserRole, i);

    const auto &sc = ThemeManager::instance().activeTheme().colors;
    if (s.ping >= 0 && s.ping < 100) {
      m_authTable->item(i, 4)->setForeground(QColor(sc.success));
    } else if (s.ping < 200) {
      m_authTable->item(i, 4)->setForeground(QColor(sc.warning));
    } else {
      m_authTable->item(i, 4)->setForeground(QColor(sc.error));
    }
  }
}

void NetworkWidget::updateAssetTable() {
  auto servers = m_serverManager->assetServers();

  std::sort(servers.begin(), servers.end(),
            [](const GW2Server &a, const GW2Server &b) {
              int pingA = (a.ping < 0 || a.ping >= 9999) ? 99999 : a.ping;
              int pingB = (b.ping < 0 || b.ping >= 9999) ? 99999 : b.ping;
              return pingA < pingB;
            });

  m_assetTable->setRowCount(servers.size());
  m_assetCountLabel->setText(QString("%1 servers found").arg(servers.size()));

  for (int i = 0; i < servers.size(); i++) {
    const auto &s = servers[i];

    auto *statusItem = new QTableWidgetItem();
    statusItem->setIcon(QIcon(statusIcon(s.ping)));
    m_assetTable->setItem(i, 0, statusItem);
    m_assetTable->setItem(i, 1, new QTableWidgetItem(s.ip));
    m_assetTable->setItem(i, 2, new QTableWidgetItem(QString::number(s.port)));
    m_assetTable->setItem(i, 3, new QTableWidgetItem(s.type));
    m_assetTable->setItem(i, 4, new QTableWidgetItem(pingToString(s.ping)));

    m_assetTable->item(i, 0)->setData(Qt::UserRole, i);

    const auto &ac = ThemeManager::instance().activeTheme().colors;
    if (s.ping >= 0 && s.ping < 100) {
      m_assetTable->item(i, 4)->setForeground(QColor(ac.success));
    } else if (s.ping < 200) {
      m_assetTable->item(i, 4)->setForeground(QColor(ac.warning));
    } else {
      m_assetTable->item(i, 4)->setForeground(QColor(ac.error));
    }
  }
}

QString NetworkWidget::pingToString(int ping) {
  if (ping < 0)
    return "...";
  if (ping >= 9999)
    return "Timeout";
  return QString::number(ping);
}

QString NetworkWidget::statusIcon(int ping) {
  if (ping < 0)
    return ":/icons/circle-gray.svg";
  if (ping >= 9999)
    return ":/icons/circle-red.svg";
  if (ping < 100)
    return ":/icons/circle-green.svg";
  if (ping < 200)
    return ":/icons/circle-yellow.svg";
  return ":/icons/circle-red.svg";
}

void NetworkWidget::onAddAuthServer() {
  // Styled input dialog
  auto *d = new QDialog(this);
  d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  d->setAttribute(Qt::WA_TranslucentBackground);
  d->setMinimumWidth(380);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  bg->setObjectName("authInputBg");
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);
  ly->setSpacing(15);
  auto *tl = new QLabel("Add Auth Server");
  UIHelpers::applyGoldTitleRole(tl);
  tl->setAlignment(Qt::AlignCenter);
  ly->addWidget(tl);
  auto *lb = new QLabel("Enter IP address:");
  UIHelpers::applyPopupLabelRole(lb);
  ly->addWidget(lb);
  auto *input = new QLineEdit();
  input->setPlaceholderText("e.g. 192.168.1.100");
  UIHelpers::applyInputFieldRole(input);
  ly->addWidget(input);
  auto *btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(12);
  auto *cancelBtn = new QPushButton("Cancel");
  cancelBtn->setMinimumHeight(36);
  UIHelpers::applyCancelStyle(cancelBtn);
  connect(cancelBtn, &QPushButton::clicked, d, &QDialog::reject);
  btnLayout->addWidget(cancelBtn);
  auto *okBtn = new QPushButton("OK");
  okBtn->setMinimumHeight(36);
  UIHelpers::applyPrimaryStyle(okBtn);
  connect(okBtn, &QPushButton::clicked, d, &QDialog::accept);
  btnLayout->addWidget(okBtn);
  ly->addLayout(btnLayout);
  UIHelpers::centerDialog(d);

  bool ok = (d->exec() == QDialog::Accepted);
  QString ip = input->text().trimmed();
  d->deleteLater();

  if (ok && !ip.isEmpty()) {
    QRegularExpression ipRegex(
        R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)");
    auto match = ipRegex.match(ip);
    if (match.hasMatch()) {
      bool validOctets = true;
      for (int i = 1; i <= 4; ++i) {
        int octet = match.captured(i).toInt();
        if (octet < 0 || octet > 255) {
          validOctets = false;
          break;
        }
      }
      if (validOctets) {
        m_serverManager->addAuthServer(ip, 6112);
      } else {
        // Styled warning dialog
        auto *d = new QDialog(this);
        d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        d->setAttribute(Qt::WA_TranslucentBackground);
        d->setMinimumWidth(300);
        auto *ol = new QVBoxLayout(d);
        ol->setContentsMargins(0, 0, 0, 0);
        auto *bg = new QWidget();
        bg->setObjectName("ipWarnBg");
        UIHelpers::applyPopupBackgroundRole(bg);
        ol->addWidget(bg);
        auto *ly = new QVBoxLayout(bg);
        ly->setContentsMargins(20, 20, 20, 20);
        auto *lb = new QLabel("IP octets must be 0-255.");
        UIHelpers::applyPopupLabelRole(lb);
        lb->setAlignment(Qt::AlignCenter);
        ly->addWidget(lb);
        auto *ok = new QPushButton("OK");
        ok->setMinimumHeight(36);
        UIHelpers::applyPrimaryStyle(ok);
        connect(ok, &QPushButton::clicked, d, &QDialog::accept);
        ly->addWidget(ok);
        d->exec();
        d->deleteLater();
      }
    } else {
      // Styled warning dialog
      auto *d = new QDialog(this);
      d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
      d->setAttribute(Qt::WA_TranslucentBackground);
      d->setMinimumWidth(320);
      auto *ol = new QVBoxLayout(d);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      bg->setObjectName("ipWarnBg2");
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      auto *lb = new QLabel("Please enter a valid IP address.");
      UIHelpers::applyPopupLabelRole(lb);
      lb->setAlignment(Qt::AlignCenter);
      ly->addWidget(lb);
      auto *ok = new QPushButton("OK");
      ok->setMinimumHeight(36);
      UIHelpers::applyPrimaryStyle(ok);
      connect(ok, &QPushButton::clicked, d, &QDialog::accept);
      ly->addWidget(ok);
      d->exec();
      d->deleteLater();
    }
  }
}

void NetworkWidget::onAddAssetServer() {
  // Styled input dialog
  auto *d = new QDialog(this);
  d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  d->setAttribute(Qt::WA_TranslucentBackground);
  d->setMinimumWidth(380);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  bg->setObjectName("assetInputBg");
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);
  ly->setSpacing(15);
  auto *tl = new QLabel("Add Asset Server");
  UIHelpers::applyGoldTitleRole(tl);
  tl->setAlignment(Qt::AlignCenter);
  ly->addWidget(tl);
  auto *lb = new QLabel("Enter IP address:");
  UIHelpers::applyPopupLabelRole(lb);
  ly->addWidget(lb);
  auto *input = new QLineEdit();
  input->setPlaceholderText("e.g. 192.168.1.100");
  UIHelpers::applyInputFieldRole(input);
  ly->addWidget(input);
  auto *btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(12);
  auto *cancelBtn = new QPushButton("Cancel");
  cancelBtn->setMinimumHeight(36);
  UIHelpers::applyCancelStyle(cancelBtn);
  connect(cancelBtn, &QPushButton::clicked, d, &QDialog::reject);
  btnLayout->addWidget(cancelBtn);
  auto *okBtn = new QPushButton("OK");
  okBtn->setMinimumHeight(36);
  UIHelpers::applyPrimaryStyle(okBtn);
  connect(okBtn, &QPushButton::clicked, d, &QDialog::accept);
  btnLayout->addWidget(okBtn);
  ly->addLayout(btnLayout);
  UIHelpers::centerDialog(d);

  bool ok = (d->exec() == QDialog::Accepted);
  QString ip = input->text().trimmed();
  d->deleteLater();

  if (ok && !ip.isEmpty()) {
    QRegularExpression ipRegex(
        R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)");
    auto match = ipRegex.match(ip);
    if (match.hasMatch()) {
      bool validOctets = true;
      for (int i = 1; i <= 4; ++i) {
        int octet = match.captured(i).toInt();
        if (octet < 0 || octet > 255) {
          validOctets = false;
          break;
        }
      }
      if (validOctets) {
        m_serverManager->addAssetServer(ip, 80);
      } else {
        // Styled warning dialog
        auto *d = new QDialog(this);
        d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        d->setAttribute(Qt::WA_TranslucentBackground);
        d->setMinimumWidth(300);
        auto *ol = new QVBoxLayout(d);
        ol->setContentsMargins(0, 0, 0, 0);
        auto *bg = new QWidget();
        bg->setObjectName("ipWarnBg3");
        UIHelpers::applyPopupBackgroundRole(bg);
        ol->addWidget(bg);
        auto *ly = new QVBoxLayout(bg);
        ly->setContentsMargins(20, 20, 20, 20);
        auto *lb = new QLabel("IP octets must be 0-255.");
        UIHelpers::applyPopupLabelRole(lb);
        lb->setAlignment(Qt::AlignCenter);
        ly->addWidget(lb);
        auto *ok = new QPushButton("OK");
        ok->setMinimumHeight(36);
        UIHelpers::applyPrimaryStyle(ok);
        connect(ok, &QPushButton::clicked, d, &QDialog::accept);
        ly->addWidget(ok);
        d->exec();
        d->deleteLater();
      }
    } else {
      // Styled warning dialog
      auto *d = new QDialog(this);
      d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
      d->setAttribute(Qt::WA_TranslucentBackground);
      d->setMinimumWidth(320);
      auto *ol = new QVBoxLayout(d);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      bg->setObjectName("ipWarnBg4");
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      auto *lb = new QLabel("Please enter a valid IP address.");
      UIHelpers::applyPopupLabelRole(lb);
      lb->setAlignment(Qt::AlignCenter);
      ly->addWidget(lb);
      auto *ok = new QPushButton("OK");
      ok->setMinimumHeight(36);
      UIHelpers::applyPrimaryStyle(ok);
      connect(ok, &QPushButton::clicked, d, &QDialog::accept);
      ly->addWidget(ok);
      d->exec();
      d->deleteLater();
    }
  }
}

void NetworkWidget::onAuthSelectionChanged() {
  int row = m_authTable->currentRow();
  if (row >= 0) {
    m_serverManager->setSelectedAuthServer(row);
    auto *server = m_serverManager->selectedAuthServer();
    if (server) {
      m_selectedAuthLabel->setText(QString("Selected: %1").arg(server->ip));
      UIHelpers::applyGoldColorRole(m_selectedAuthLabel);
    }
  } else {
    m_serverManager->setSelectedAuthServer(-1);
    m_selectedAuthLabel->setText("Selected: Default");
    UIHelpers::applySuccessColorRole(m_selectedAuthLabel);
  }
  updateLaunchArgsInfo();
}

void NetworkWidget::onAssetSelectionChanged() {
  int row = m_assetTable->currentRow();
  if (row >= 0) {
    m_serverManager->setSelectedAssetServer(row);
    auto *server = m_serverManager->selectedAssetServer();
    if (server) {
      m_selectedAssetLabel->setText(QString("Selected: %1").arg(server->ip));
      UIHelpers::applyGoldColorRole(m_selectedAssetLabel);
    }
  } else {
    m_serverManager->setSelectedAssetServer(-1);
    m_selectedAssetLabel->setText("Selected: Default");
    UIHelpers::applySuccessColorRole(m_selectedAssetLabel);
  }
  updateLaunchArgsInfo();
}

void NetworkWidget::updateLaunchArgsInfo() {
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

  const auto &tc = ThemeManager::instance().activeTheme().colors;
  if (args.isEmpty()) {
    m_launchArgsLabel->setText(
        "No custom servers selected - using GW2 defaults");
    m_launchArgsLabel->setStyleSheet(
        QString("color: %1; font-family: 'Consolas', monospace; padding: 8px; "
                "background: %2; border-radius: 4px;")
            .arg(tc.success, tc.windowBg));
  } else {
    QString argsText = "Game will launch with:\n" + args.join("\n");
    m_launchArgsLabel->setText(argsText);
    m_launchArgsLabel->setStyleSheet(
        QString("color: %1; font-family: 'Consolas', monospace; padding: 8px; "
                "background: %2; border-radius: 4px;")
            .arg(tc.textAccent, tc.windowBg));
  }
}

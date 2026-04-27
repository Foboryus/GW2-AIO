/**
 * @file AccountTabWidget.cpp
 * @brief Account tab for ProfileEditor
 *
 * Shows GW2 account summary and character list from the API.
 * Read-only — all data comes from /v2/account and /v2/characters.
 *
 * DO NOT ADD:
 * - API key input logic (belongs in LoginTabWidget)
 * - Inline styles (use UIHelpers role-based styling)
 */

#include "AccountTabWidget.h"

#include <QDateTime>
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/APIKeyManager.h"
#include "core/DataService.h"
#include "core/GW2APIClient.h"
#include "core/ProfileManager.h" // For AccountProfile
#include "ui/UIHelpers.h"

AccountTabWidget::AccountTabWidget(AccountProfile &profile,
                                   DataService *dataService, QWidget *parent)
    : QWidget(parent), m_profile(profile), m_dataService(dataService),
      m_apiKeyManager(dataService ? dataService->apiKeyManager() : nullptr),
      m_apiClient(dataService ? dataService->apiClient() : nullptr) {
  setupUI();
}

void AccountTabWidget::setupUI() {
  auto *layout = new QVBoxLayout(this);

  // No-key placeholder (shown when no API key is configured)
  m_noKeyPlaceholder = new QWidget();
  auto *placeholderLayout = new QVBoxLayout(m_noKeyPlaceholder);
  placeholderLayout->setAlignment(Qt::AlignCenter);

  auto *noKeyIcon = new QLabel();
  UIHelpers::setThemedPixmap(noKeyIcon, "lock", 48);
  noKeyIcon->setAlignment(Qt::AlignCenter);
  placeholderLayout->addWidget(noKeyIcon);

  auto *noKeyLabel = new QLabel(
      "No API key configured\n\n"
      "Go to the Login tab to enter your GW2 API key.\n"
      "This will enable account info and character display.");
  noKeyLabel->setWordWrap(true);
  noKeyLabel->setAlignment(Qt::AlignCenter);
  UIHelpers::applyHintRole(noKeyLabel);
  placeholderLayout->addWidget(noKeyLabel);

  placeholderLayout->addSpacing(12);

  auto *placeholderRefreshBtn =
      new QPushButton(UIHelpers::themedIcon("refresh"), "Refresh");
  UIHelpers::applyNeutralStyle(placeholderRefreshBtn);
  placeholderRefreshBtn->setToolTip(
      "Check if an API key has been added in the Login tab");
  connect(placeholderRefreshBtn, &QPushButton::clicked, this,
          &AccountTabWidget::load);
  placeholderLayout->addWidget(placeholderRefreshBtn, 0, Qt::AlignCenter);

  layout->addWidget(m_noKeyPlaceholder);

  // Account info container (hidden until data is loaded)
  m_accountInfoContainer = new QWidget();
  auto *infoLayout = new QVBoxLayout(m_accountInfoContainer);
  infoLayout->setContentsMargins(0, 0, 0, 0);

  // Refresh button at top
  auto *topBar = new QHBoxLayout();
  m_statusLabel = new QLabel("Loading account data...");
  UIHelpers::applyStatusRole(m_statusLabel);
  topBar->addWidget(m_statusLabel, 1);

  m_refreshBtn =
      new QPushButton(UIHelpers::themedIcon("refresh"), "Refresh");
  UIHelpers::applyNeutralStyle(m_refreshBtn);
  m_refreshBtn->setToolTip("Fetch latest data from GW2 API");
  connect(m_refreshBtn, &QPushButton::clicked, this,
          &AccountTabWidget::refreshAccountData);
  topBar->addWidget(m_refreshBtn);
  infoLayout->addLayout(topBar);

  // Account summary group
  auto *accountGroup = new QGroupBox("Account Summary");
  auto *accountLayout = new QVBoxLayout(accountGroup);

  m_accountNameLabel = new QLabel();
  UIHelpers::applyLabelRole(m_accountNameLabel);
  accountLayout->addWidget(m_accountNameLabel);

  m_accountAgeLabel = new QLabel();
  UIHelpers::applySecondaryRole(m_accountAgeLabel);
  accountLayout->addWidget(m_accountAgeLabel);

  m_expansionsLabel = new QLabel();
  UIHelpers::applySecondaryRole(m_expansionsLabel);
  accountLayout->addWidget(m_expansionsLabel);

  // Stats row
  auto *statsRow = new QHBoxLayout();

  m_apLabel = new QLabel();
  UIHelpers::applyLabelRole(m_apLabel);
  statsRow->addWidget(m_apLabel);

  m_wvwLabel = new QLabel();
  UIHelpers::applyLabelRole(m_wvwLabel);
  statsRow->addWidget(m_wvwLabel);

  m_fractalLabel = new QLabel();
  UIHelpers::applyLabelRole(m_fractalLabel);
  statsRow->addWidget(m_fractalLabel);

  m_commanderLabel = new QLabel();
  UIHelpers::applyLabelRole(m_commanderLabel);
  statsRow->addWidget(m_commanderLabel);

  statsRow->addStretch();
  accountLayout->addLayout(statsRow);

  infoLayout->addWidget(accountGroup);

  // Character list group
  auto *charGroup = new QGroupBox("Characters");
  auto *charLayout = new QVBoxLayout(charGroup);

  m_characterTable = new QTableWidget();
  m_characterTable->setColumnCount(7);
  m_characterTable->setHorizontalHeaderLabels(
      {"Name", "Level", "Race", "Profession", "Deaths", "Playtime",
       "Next Birthday"});
  m_characterTable->horizontalHeader()->setStretchLastSection(true);
  m_characterTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_characterTable->verticalHeader()->setVisible(false);
  m_characterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_characterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_characterTable->setAlternatingRowColors(true);
  m_characterTable->setSortingEnabled(true);
  charLayout->addWidget(m_characterTable);

  infoLayout->addWidget(charGroup);

  // Badges placeholder
  auto *badgeGroup = new QGroupBox("Profile Badges");
  auto *badgeLayout = new QVBoxLayout(badgeGroup);
  auto *badgePlaceholder =
      new QLabel("Badge selection coming in Phase 2.\n"
                 "You'll be able to choose up to 3 badges to display "
                 "on your profile card and tray menu.");
  badgePlaceholder->setWordWrap(true);
  UIHelpers::applyHintRole(badgePlaceholder);
  badgeLayout->addWidget(badgePlaceholder);
  infoLayout->addWidget(badgeGroup);

  m_accountInfoContainer->setVisible(false);
  layout->addWidget(m_accountInfoContainer);

  // Connect API signals for async data
  if (m_apiClient) {
    connect(m_apiClient, &GW2APIClient::accountFetched, this,
            [this](const GW2APIClient::AccountInfo &account) {
              showAccountInfo(account.name, account.access, account.wvwRank,
                              account.fractalLevel, account.dailyAP,
                              account.monthlyAP, account.commander,
                              QString()); // createdAt not in struct
              m_statusLabel->setText("Account data loaded");
              UIHelpers::applySuccessColorRole(m_statusLabel);
            });

    connect(m_apiClient, &GW2APIClient::charactersFetched, this,
            [this](const QList<GW2APIClient::Character> &characters) {
              m_characterTable->setSortingEnabled(false);
              m_characterTable->setRowCount(characters.size());
              for (int i = 0; i < characters.size(); ++i) {
                const auto &ch = characters[i];
                m_characterTable->setItem(
                    i, 0, new QTableWidgetItem(ch.name));
                auto *levelItem =
                    new QTableWidgetItem(QString::number(ch.level));
                levelItem->setTextAlignment(Qt::AlignCenter);
                m_characterTable->setItem(i, 1, levelItem);
                m_characterTable->setItem(
                    i, 2, new QTableWidgetItem(ch.race));
                m_characterTable->setItem(
                    i, 3, new QTableWidgetItem(ch.profession));
                auto *deathsItem =
                    new QTableWidgetItem(QString::number(ch.deaths));
                deathsItem->setTextAlignment(Qt::AlignCenter);
                m_characterTable->setItem(i, 4, deathsItem);

                // Playtime — convert age (seconds) to human readable
                int totalSecs = ch.age;
                int hours = totalSecs / 3600;
                int mins = (totalSecs % 3600) / 60;
                QString playtime;
                if (hours >= 24) {
                  int days = hours / 24;
                  hours = hours % 24;
                  playtime = QString("%1d %2h").arg(days).arg(hours);
                } else {
                  playtime = QString("%1h %2m").arg(hours).arg(mins);
                }
                auto *playtimeItem = new QTableWidgetItem(playtime);
                playtimeItem->setData(Qt::UserRole, totalSecs); // Sort by raw
                playtimeItem->setTextAlignment(Qt::AlignCenter);
                m_characterTable->setItem(i, 5, playtimeItem);

                // Next Birthday — from created date
                QDateTime created =
                    QDateTime::fromString(ch.created, Qt::ISODate);
                QString birthdayText = "—";
                if (created.isValid()) {
                  QDate today = QDate::currentDate();
                  QDate nextBday(today.year(), created.date().month(),
                                 created.date().day());
                  if (nextBday < today) {
                    nextBday = nextBday.addYears(1);
                  }
                  qint64 daysUntil = today.daysTo(nextBday);
                  int charAge = created.date().daysTo(today) / 365;
                  if (daysUntil == 0) {
                    birthdayText =
                        QString("🎂 Today! (Year %1)").arg(charAge + 1);
                  } else {
                    birthdayText = QString("%1 days (Year %2)")
                                       .arg(daysUntil)
                                       .arg(charAge + 1);
                  }
                }
                auto *bdayItem = new QTableWidgetItem(birthdayText);
                bdayItem->setTextAlignment(Qt::AlignCenter);
                m_characterTable->setItem(i, 6, bdayItem);
              }
              m_characterTable->setSortingEnabled(true);
              m_characterTable->resizeColumnsToContents();
            });

    connect(m_apiClient, &GW2APIClient::errorOccurred, this,
            [this](const QString &endpoint, const QString &error) {
              if (endpoint == "account" || endpoint.startsWith("characters")) {
                m_statusLabel->setText("Error: " + error);
                UIHelpers::applyErrorColorRole(m_statusLabel);
                m_refreshBtn->setEnabled(true);
              }
            });
  }
}

void AccountTabWidget::load() {
  if (!m_apiKeyManager || !m_apiKeyManager->hasKey(m_profile.id)) {
    showNoKeyPlaceholder();
    return;
  }

  // Has key — show account info container
  m_noKeyPlaceholder->setVisible(false);
  m_accountInfoContainer->setVisible(true);

  refreshAccountData();
}

void AccountTabWidget::save() {
  // No-op: account data is read-only from API
}

void AccountTabWidget::refreshAccountData() {
  if (!m_apiKeyManager || !m_apiClient)
    return;

  QString key = m_apiKeyManager->retrieveKey(m_profile.id);
  if (key.isEmpty()) {
    showNoKeyPlaceholder();
    return;
  }

  m_statusLabel->setText("Fetching account data...");
  UIHelpers::applyStatusRole(m_statusLabel);
  m_refreshBtn->setEnabled(false);

  // Set the key and fetch account + characters
  m_apiClient->setApiKey(key);
  m_apiClient->fetchAccount();
  m_apiClient->fetchCharacters();

  // Re-enable refresh after a delay (regardless of success/failure)
  QTimer::singleShot(3000, this, [this]() {
    if (m_refreshBtn)
      m_refreshBtn->setEnabled(true);
  });
}

void AccountTabWidget::showNoKeyPlaceholder() {
  m_noKeyPlaceholder->setVisible(true);
  m_accountInfoContainer->setVisible(false);
}

void AccountTabWidget::showAccountInfo(const QString &accountName,
                                       const QStringList &access, int wvwRank,
                                       int fractalLevel, int dailyAp,
                                       int monthlyAp, bool commander,
                                       const QString &createdAt) {
  Q_UNUSED(createdAt);

  m_accountNameLabel->setText(
      QString("<b style='font-size: 14px;'>%1</b>").arg(accountName));

  // Parse access for owned expansions
  QStringList expansions;
  if (access.contains("GuildWars2"))
    expansions << "Core";
  if (access.contains("HeartOfThorns"))
    expansions << "HoT";
  if (access.contains("PathOfFire"))
    expansions << "PoF";
  if (access.contains("EndOfDragons"))
    expansions << "EoD";
  if (access.contains("SecretsOfTheObscure"))
    expansions << "SotO";
  if (access.contains("JanthirWilds"))
    expansions << "JW";
  m_expansionsLabel->setText("Expansions: " + expansions.join(" | "));

  m_apLabel->setText(QString("AP: %1 daily / %2 monthly")
                         .arg(dailyAp)
                         .arg(monthlyAp));
  m_wvwLabel->setText(QString("WvW Rank: %1").arg(wvwRank));
  m_fractalLabel->setText(QString("Fractal Level: %1").arg(fractalLevel));
  m_commanderLabel->setText(commander ? "Commander: Yes" : "Commander: No");
}

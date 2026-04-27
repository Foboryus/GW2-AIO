#pragma once

/**
 * @brief Account tab for ProfileEditor
 *
 * Displays GW2 account info and character list from the API.
 * Requires a valid API key to be configured in the Login tab.
 *
 * DO NOT ADD:
 * - API key input (belongs in LoginTabWidget)
 * - Inline implementations (use AccountTabWidget.cpp)
 */

#include <QMap>
#include <QWidget>

class QLabel;
class QTableWidget;
class QPushButton;
struct AccountProfile;
class DataService;
class APIKeyManager;
class GW2APIClient;
class QFlowLayout;

class AccountTabWidget : public QWidget {
  Q_OBJECT

public:
  explicit AccountTabWidget(AccountProfile &profile,
                            DataService *dataService = nullptr,
                            QWidget *parent = nullptr);

  /**
   * @brief Load account data from cache or API
   */
  void load();

  /**
   * @brief Save is a no-op (account data is read-only)
   */
  void save();

signals:
  void modified();

private:
  void setupUI();
  void refreshAccountData();
  void showNoKeyPlaceholder();
  void showAccountInfo(const QString &accountName, const QStringList &access,
                       int wvwRank, int fractalLevel, int dailyAp,
                       int monthlyAp, bool commander,
                       const QString &createdAt);
  void setupBadgeSelection();
  void updateBadgeCardStates();
  void onBadgeToggled(const QString &badgeId, bool checked);

  AccountProfile &m_profile;
  DataService *m_dataService = nullptr;
  APIKeyManager *m_apiKeyManager = nullptr;
  GW2APIClient *m_apiClient = nullptr;

  // UI elements
  QLabel *m_statusLabel = nullptr;
  QWidget *m_accountInfoContainer = nullptr;
  QLabel *m_accountNameLabel = nullptr;
  QLabel *m_accountAgeLabel = nullptr;
  QLabel *m_expansionsLabel = nullptr;
  QLabel *m_apLabel = nullptr;
  QLabel *m_wvwLabel = nullptr;
  QLabel *m_fractalLabel = nullptr;
  QLabel *m_commanderLabel = nullptr;
  QTableWidget *m_characterTable = nullptr;
  QPushButton *m_refreshBtn = nullptr;

  // Placeholder for no-key state
  QWidget *m_noKeyPlaceholder = nullptr;

  // Badge selection
  QWidget *m_badgeGrid = nullptr;
  QMap<QString, QPushButton *> m_badgeCards;
};

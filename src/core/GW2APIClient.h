#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>


/**
 * @brief GW2 API client for account and game data
 *
 * DO NOT ADD:
 * - Inline implementations (use GW2APIClient.cpp)
 */
class GW2APIClient : public QObject {
  Q_OBJECT

public:
  explicit GW2APIClient(QObject *parent = nullptr);

  /**
   * @brief Set API key (from account.arena.net)
   */
  void setApiKey(const QString &key);
  bool hasApiKey() const { return !m_apiKey.isEmpty(); }

  // Account endpoints
  void fetchAccount();
  void fetchCharacters();
  void fetchWallet();
  void fetchBank();
  void fetchAchievements();

  // Game data endpoints (no key required)
  void fetchMaps();
  void fetchItems(const QList<int> &ids);
  void fetchColors();
  void fetchBuild(); // Check current GW2 build number

  /**
   * @brief Validate an API key by fetching /v2/tokeninfo
   * @param apiKey The key to validate (uses this key, not the stored one)
   */
  void fetchTokenInfo(const QString &apiKey);

  /**
   * @brief Generic API request
   */
  void request(const QString &endpoint, bool authenticated = true);

  // Cached data access
  struct AccountInfo {
    QString id;
    QString name;
    int age = 0; // Account age in seconds
    int worldId = 0;
    QStringList guilds;
    QStringList access; // "PlayForFree", "GuildWars2", "HeartOfThorns", etc.
    bool commander = false;
    int fractalLevel = 0;
    int dailyAP = 0;
    int monthlyAP = 0;
    int wvwRank = 0;
  };
  const AccountInfo &accountInfo() const { return m_account; }

  struct Character {
    QString name;
    QString race;
    QString profession;
    int level = 0;
    int age = 0;
    QString guild;
    int deaths = 0;
    QString created; // ISO 8601 datetime string
  };
  const QList<Character> &characters() const { return m_characters; }

  struct WalletCurrency {
    int id;
    QString name;
    int value;
  };
  const QList<WalletCurrency> &wallet() const { return m_wallet; }

signals:
  void accountFetched(const AccountInfo &account);
  void charactersFetched(const QList<Character> &characters);
  void walletFetched(const QList<WalletCurrency> &wallet);
  void buildFetched(int buildId); // Emitted with current GW2 build number
  void tokenInfoFetched(const QStringList &permissions,
                        const QString &tokenName, const QString &apiKey);
  void requestComplete(const QString &endpoint, const QJsonDocument &data);
  void errorOccurred(const QString &endpoint, const QString &error);

private slots:
  void onReplyFinished(QNetworkReply *reply);

private:
  QNetworkRequest makeRequest(const QString &endpoint, bool authenticated);
  void parseAccount(const QJsonObject &json);
  void parseCharacters(const QJsonArray &json);
  void parseWallet(const QJsonArray &json);
  void parseTokenInfo(const QJsonObject &json, const QString &apiKey);

  QNetworkAccessManager *m_network;
  QString m_apiKey;
  QString m_baseUrl = "https://api.guildwars2.com/v2";

  AccountInfo m_account;
  QList<Character> m_characters;
  QList<WalletCurrency> m_wallet;

  // Currency ID to name mapping
  QMap<int, QString> m_currencyNames;

  int m_lastBuildId = 0; // Cache last fetched build

  // --- Rate Limiter (Token Bucket) ---
  // GW2 API limit: ~300 requests/minute, ~600 tokens/minute
  // We use 300 tokens max, refill 5/sec for safety margin
  static constexpr int BUCKET_MAX = 300;
  static constexpr int REFILL_RATE = 5;       // tokens per second
  static constexpr int REFILL_INTERVAL = 200; // ms between refill ticks

  int m_tokens = BUCKET_MAX;
  QElapsedTimer m_lastRefill;
  QTimer *m_refillTimer = nullptr;

  struct QueuedRequest {
    QNetworkRequest request;
    bool isTokenInfo = false;
  };
  QQueue<QueuedRequest> m_requestQueue;

  bool tryConsumeToken();
  void refillTokens();
  void processQueue();
  void enqueueOrSend(const QNetworkRequest &req);
};

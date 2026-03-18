#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
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
  void requestComplete(const QString &endpoint, const QJsonDocument &data);
  void errorOccurred(const QString &endpoint, const QString &error);

private slots:
  void onReplyFinished(QNetworkReply *reply);

private:
  QNetworkRequest makeRequest(const QString &endpoint, bool authenticated);
  void parseAccount(const QJsonObject &json);
  void parseCharacters(const QJsonArray &json);
  void parseWallet(const QJsonArray &json);

  QNetworkAccessManager *m_network;
  QString m_apiKey;
  QString m_baseUrl = "https://api.guildwars2.com/v2";

  AccountInfo m_account;
  QList<Character> m_characters;
  QList<WalletCurrency> m_wallet;

  // Currency ID to name mapping
  QMap<int, QString> m_currencyNames;

  int m_lastBuildId = 0; // Cache last fetched build
};

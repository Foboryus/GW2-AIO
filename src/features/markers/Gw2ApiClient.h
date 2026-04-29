#pragma once

/**
 * @brief Rate-limited HTTP client for the GW2 API (api.guildwars2.com/v2/)
 *
 * Provides typed accessors for account info and achievement progress.
 * Uses a token-bucket rate limiter matching GW2's server-side limits
 * (300 burst, 5 tokens/sec refill). Queues requests when tokens are
 * exhausted and backs off on HTTP 429.
 *
 * Owned by MarkerController (dependency injection).
 *
 * DO NOT ADD:
 * - UI code (belongs in Phase 3 settings page)
 * - Marker management logic (belongs in MarkerManager)
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

struct PendingApiRequest {
  QUrl url;
  QString endpoint; // routing key for signal dispatch
};

class Gw2ApiClient : public QObject {
  Q_OBJECT

public:
  explicit Gw2ApiClient(QObject *parent = nullptr);

  /// @brief Set the API key for authenticated requests
  void setApiKey(const QString &key);

  /// @brief Check if a (non-empty) API key is set
  bool hasApiKey() const { return !m_apiKey.isEmpty(); }

  /// @brief Fetch account info (validates key, returns account name etc.)
  void fetchAccountInfo();

  /// @brief Fetch achievement progress for the authenticated account
  void fetchAchievementProgress();

  /// @brief Current queue depth (for diagnostics)
  int pendingRequests() const { return m_requestQueue.size(); }

signals:
  /// @brief Emitted when /v2/account responds successfully
  void accountInfoReady(const QJsonObject &data);

  /// @brief Emitted when /v2/account/achievements responds successfully
  void achievementProgressReady(const QJsonArray &data);

  /// @brief Emitted on any API error (network, auth, rate limit exhausted)
  void apiError(const QString &endpoint, const QString &errorMessage);

private slots:
  void onRefillTick();
  void processQueue();
  void onReplyFinished(QNetworkReply *reply);

private:
  void enqueueRequest(const QUrl &url, const QString &endpoint);
  void sendRequest(const PendingApiRequest &req);

  static const QString BASE_URL;

  // Network
  QNetworkAccessManager *m_nam;
  QString m_apiKey;

  // Token bucket rate limiter
  int m_tokens = 300; // Current token count (max 300)
  static const int MAX_TOKENS = 300;
  QTimer *m_refillTimer; // 200ms interval → +1 token (5/sec)

  // Request queue
  QQueue<PendingApiRequest> m_requestQueue;
};

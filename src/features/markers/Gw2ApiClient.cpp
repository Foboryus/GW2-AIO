/**
 * @file Gw2ApiClient.cpp
 * @brief Rate-limited HTTP client for the GW2 API
 *
 * Token bucket rate limiter: 300 burst, 5 tokens/sec refill.
 * On HTTP 429: 5-second backoff + re-enqueue.
 * All responses dispatched via signals.
 */

#include "Gw2ApiClient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

const QString Gw2ApiClient::BASE_URL =
    QStringLiteral("https://api.guildwars2.com/v2/");

Gw2ApiClient::Gw2ApiClient(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)),
      m_refillTimer(new QTimer(this)) {

  // Token refill: +1 every 200ms = 5/sec (matches GW2 API)
  m_refillTimer->setInterval(200);
  connect(m_refillTimer, &QTimer::timeout, this, &Gw2ApiClient::onRefillTick);
  m_refillTimer->start();

  // Network reply handling
  connect(m_nam, &QNetworkAccessManager::finished, this,
          &Gw2ApiClient::onReplyFinished);
}

void Gw2ApiClient::setApiKey(const QString &key) {
  m_apiKey = key.trimmed();
  if (m_apiKey.isEmpty()) {
    qInfo() << "Gw2ApiClient: API key cleared";
  } else {
    qInfo() << "Gw2ApiClient: API key set (" << m_apiKey.left(8) + "..." << ")";
  }
}

void Gw2ApiClient::fetchAccountInfo() {
  if (!hasApiKey()) {
    emit apiError("account", "No API key set");
    return;
  }
  QUrl url(BASE_URL + "account");
  enqueueRequest(url, "account");
}

void Gw2ApiClient::fetchAchievementProgress() {
  if (!hasApiKey()) {
    emit apiError("account/achievements", "No API key set");
    return;
  }
  QUrl url(BASE_URL + "account/achievements");
  enqueueRequest(url, "account/achievements");
}

// --- Rate limiter ---

void Gw2ApiClient::enqueueRequest(const QUrl &url, const QString &endpoint) {
  m_requestQueue.enqueue({url, endpoint});
  processQueue();
}

void Gw2ApiClient::processQueue() {
  while (!m_requestQueue.isEmpty() && m_tokens > 0) {
    PendingApiRequest req = m_requestQueue.dequeue();
    --m_tokens;
    sendRequest(req);
  }
}

void Gw2ApiClient::onRefillTick() {
  if (m_tokens < MAX_TOKENS) {
    ++m_tokens;
  }
  // Try to drain queued requests
  if (!m_requestQueue.isEmpty()) {
    processQueue();
  }
}

void Gw2ApiClient::sendRequest(const PendingApiRequest &req) {
  QNetworkRequest netReq(req.url);
  netReq.setRawHeader("Authorization",
                      QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8());
  netReq.setRawHeader("Accept", "application/json");
  netReq.setAttribute(QNetworkRequest::User, req.endpoint);

  QNetworkReply *reply = m_nam->get(netReq);
  Q_UNUSED(reply);
}

void Gw2ApiClient::onReplyFinished(QNetworkReply *reply) {
  reply->deleteLater();

  QString endpoint =
      reply->request().attribute(QNetworkRequest::User).toString();
  int statusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  // Handle rate limit
  if (statusCode == 429) {
    qWarning() << "Gw2ApiClient: Rate limited (429), backing off 5s for"
               << endpoint;
    // Re-enqueue after 5 second backoff
    QUrl url = reply->url();
    QTimer::singleShot(5000, this, [this, url, endpoint]() {
      m_requestQueue.enqueue({url, endpoint});
      processQueue();
    });
    return;
  }

  // Handle network error
  if (reply->error() != QNetworkReply::NoError) {
    QString errMsg =
        QStringLiteral("HTTP %1: %2").arg(statusCode).arg(reply->errorString());
    qWarning() << "Gw2ApiClient: Error for" << endpoint << "—" << errMsg;
    emit apiError(endpoint, errMsg);
    return;
  }

  // Parse JSON response
  QByteArray data = reply->readAll();
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "Gw2ApiClient: JSON parse error for" << endpoint << "—"
               << parseError.errorString();
    emit apiError(endpoint, "JSON parse error: " + parseError.errorString());
    return;
  }

  // Dispatch by endpoint
  if (endpoint == "account") {
    if (doc.isObject()) {
      qInfo() << "Gw2ApiClient: Account info received";
      emit accountInfoReady(doc.object());
    } else {
      emit apiError(endpoint, "Expected JSON object");
    }
  } else if (endpoint == "account/achievements") {
    if (doc.isArray()) {
      qInfo() << "Gw2ApiClient: Achievement progress received ("
              << doc.array().size() << "entries)";
      emit achievementProgressReady(doc.array());
    } else {
      emit apiError(endpoint, "Expected JSON array");
    }
  } else {
    qWarning() << "Gw2ApiClient: Unknown endpoint" << endpoint;
  }
}

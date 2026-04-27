/**
 * @file GW2APIClient.cpp
 * @brief GW2 API client for account and game data
 *
 * DO NOT ADD:
 * - Profile management (belongs in ProfileManager)
 * - UI code
 */

#include "GW2APIClient.h"

#include <QDebug>

GW2APIClient::GW2APIClient(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {
  connect(m_network, &QNetworkAccessManager::finished, this,
          &GW2APIClient::onReplyFinished);

  // Initialize rate limiter
  m_lastRefill.start();
  m_refillTimer = new QTimer(this);
  m_refillTimer->setInterval(REFILL_INTERVAL);
  connect(m_refillTimer, &QTimer::timeout, this, &GW2APIClient::processQueue);

  // Common currency names
  m_currencyNames[1] = "Coin";
  m_currencyNames[2] = "Karma";
  m_currencyNames[3] = "Laurels";
  m_currencyNames[4] = "Gems";
  m_currencyNames[5] = "Ascalonian Tears";
  m_currencyNames[15] = "Badges of Honor";
  m_currencyNames[16] = "Guild Commendations";
  m_currencyNames[23] = "Spirit Shards";
  m_currencyNames[32] = "Unbound Magic";
  m_currencyNames[45] = "Volatile Magic";
}

void GW2APIClient::setApiKey(const QString &key) {
  m_apiKey = key;
  qInfo() << "GW2 API key set";
}

QNetworkRequest GW2APIClient::makeRequest(const QString &endpoint,
                                          bool authenticated) {
  QString url = m_baseUrl + "/" + endpoint;

  if (authenticated && !m_apiKey.isEmpty()) {
    if (endpoint.contains("?")) {
      url += "&access_token=" + m_apiKey;
    } else {
      url += "?access_token=" + m_apiKey;
    }
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, "GW2AIO/1.0");
  request.setRawHeader("Accept", "application/json");

  return request;
}

void GW2APIClient::request(const QString &endpoint, bool authenticated) {
  QNetworkRequest req = makeRequest(endpoint, authenticated);
  req.setAttribute(QNetworkRequest::User, endpoint);
  enqueueOrSend(req);
}

void GW2APIClient::fetchAccount() { request("account"); }

void GW2APIClient::fetchCharacters() { request("characters?page=0"); }

void GW2APIClient::fetchWallet() { request("account/wallet"); }

void GW2APIClient::fetchBank() { request("account/bank"); }

void GW2APIClient::fetchAchievements() { request("account/achievements"); }

void GW2APIClient::fetchMaps() { request("maps", false); }

void GW2APIClient::fetchItems(const QList<int> &ids) {
  QStringList idStrs;
  for (int id : ids)
    idStrs << QString::number(id);
  request("items?ids=" + idStrs.join(","), false);
}

void GW2APIClient::fetchColors() { request("colors", false); }

void GW2APIClient::fetchBuild() { request("build", false); }

void GW2APIClient::fetchTokenInfo(const QString &apiKey) {
  // One-off request with the provided key (not the stored one)
  QString url = m_baseUrl + "/tokeninfo?access_token=" + apiKey;
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "GW2AIO/1.0");
  req.setRawHeader("Accept", "application/json");
  req.setAttribute(QNetworkRequest::User, QStringLiteral("tokeninfo"));
  // Store the API key in a custom attribute so we can match the response
  req.setAttribute(static_cast<QNetworkRequest::Attribute>(
                       QNetworkRequest::User + 1),
                   apiKey);
  enqueueOrSend(req);
}

void GW2APIClient::onReplyFinished(QNetworkReply *reply) {
  reply->deleteLater();

  QString endpoint =
      reply->request().attribute(QNetworkRequest::User).toString();

  if (reply->error() != QNetworkReply::NoError) {
    emit errorOccurred(endpoint, reply->errorString());
    return;
  }

  QByteArray data = reply->readAll();
  QJsonDocument doc = QJsonDocument::fromJson(data);

  // Parse specific endpoints
  if (endpoint == "account") {
    parseAccount(doc.object());
    emit accountFetched(m_account);
  } else if (endpoint.startsWith("characters")) {
    parseCharacters(doc.array());
    emit charactersFetched(m_characters);
  } else if (endpoint == "account/wallet") {
    parseWallet(doc.array());
    emit walletFetched(m_wallet);
  } else if (endpoint == "build") {
    m_lastBuildId = doc.object()["id"].toInt();
    emit buildFetched(m_lastBuildId);
    qInfo() << "GW2 API build:" << m_lastBuildId;
  } else if (endpoint == "tokeninfo") {
    // Extract the API key from the custom request attribute
    QString apiKey =
        reply->request()
            .attribute(static_cast<QNetworkRequest::Attribute>(
                QNetworkRequest::User + 1))
            .toString();
    parseTokenInfo(doc.object(), apiKey);
  }

  emit requestComplete(endpoint, doc);
}

void GW2APIClient::parseAccount(const QJsonObject &json) {
  m_account.id = json["id"].toString();
  m_account.name = json["name"].toString();
  m_account.age = json["age"].toInt();
  m_account.worldId = json["world"].toInt();
  m_account.commander = json["commander"].toBool();
  m_account.fractalLevel = json["fractal_level"].toInt();
  m_account.dailyAP = json["daily_ap"].toInt();
  m_account.monthlyAP = json["monthly_ap"].toInt();
  m_account.wvwRank = json["wvw_rank"].toInt();

  m_account.guilds.clear();
  for (const QJsonValue &val : json["guilds"].toArray()) {
    m_account.guilds.append(val.toString());
  }

  m_account.access.clear();
  for (const QJsonValue &val : json["access"].toArray()) {
    m_account.access.append(val.toString());
  }
}

void GW2APIClient::parseCharacters(const QJsonArray &json) {
  m_characters.clear();

  for (const QJsonValue &val : json) {
    QJsonObject obj = val.toObject();
    Character c;
    c.name = obj["name"].toString();
    c.race = obj["race"].toString();
    c.profession = obj["profession"].toString();
    c.level = obj["level"].toInt();
    c.age = obj["age"].toInt();
    c.guild = obj["guild"].toString();
    c.deaths = obj["deaths"].toInt();
    c.created = obj["created"].toString();

    m_characters.append(c);
  }
}

void GW2APIClient::parseWallet(const QJsonArray &json) {
  m_wallet.clear();

  for (const QJsonValue &val : json) {
    QJsonObject obj = val.toObject();
    WalletCurrency w;
    w.id = obj["id"].toInt();
    w.value = obj["value"].toInt();
    w.name = m_currencyNames.value(w.id, QString("Currency %1").arg(w.id));

    m_wallet.append(w);
  }
}

void GW2APIClient::parseTokenInfo(const QJsonObject &json,
                                  const QString &apiKey) {
  QString name = json["name"].toString();
  QStringList permissions;

  QJsonArray permsArray = json["permissions"].toArray();
  for (const QJsonValue &val : permsArray) {
    permissions.append(val.toString());
  }

  qInfo() << "GW2 API tokeninfo: name=" << name
          << "permissions=" << permissions;
  emit tokenInfoFetched(permissions, name, apiKey);
}

// ---------------------------------------------------------------------------
// Rate Limiter — Token Bucket
// ---------------------------------------------------------------------------

bool GW2APIClient::tryConsumeToken() {
  refillTokens();
  if (m_tokens > 0) {
    --m_tokens;
    return true;
  }
  return false;
}

void GW2APIClient::refillTokens() {
  qint64 elapsed = m_lastRefill.elapsed();
  if (elapsed < REFILL_INTERVAL)
    return;

  // Add tokens proportional to elapsed time
  int tokensToAdd =
      static_cast<int>((elapsed * REFILL_RATE) / 1000); // 5 tokens/sec
  if (tokensToAdd > 0) {
    m_tokens = qMin(m_tokens + tokensToAdd, BUCKET_MAX);
    m_lastRefill.restart();
  }
}

void GW2APIClient::enqueueOrSend(const QNetworkRequest &req) {
  if (tryConsumeToken()) {
    m_network->get(req);
  } else {
    // Queue the request and start the timer if not running
    QueuedRequest queued;
    queued.request = req;
    m_requestQueue.enqueue(queued);
    if (!m_refillTimer->isActive()) {
      m_refillTimer->start();
    }
    qDebug() << "GW2APIClient: Rate limited, queued request. Queue size:"
             << m_requestQueue.size();
  }
}

void GW2APIClient::processQueue() {
  refillTokens();

  while (!m_requestQueue.isEmpty() && tryConsumeToken()) {
    QueuedRequest queued = m_requestQueue.dequeue();
    m_network->get(queued.request);
  }

  // Stop timer when queue is empty
  if (m_requestQueue.isEmpty()) {
    m_refillTimer->stop();
  }
}

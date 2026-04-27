/**
 * @file APICache.cpp
 * @brief Simple disk cache for GW2 API responses
 *
 * Stores JSON files in api_cache/{profileId}/{endpoint}.json
 * with file modification time used for TTL checking.
 *
 * DO NOT ADD:
 * - Network logic (belongs in GW2APIClient)
 * - UI code (belongs in widgets)
 */

#include "APICache.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

APICache::APICache(const QString &cacheDir) : m_cacheDir(cacheDir) {}

bool APICache::isValid(const QString &profileId, const QString &endpoint,
                       int ttlSeconds) const {
  QString path = cachePath(profileId, endpoint);
  QFileInfo info(path);

  if (!info.exists() || info.size() == 0) {
    return false;
  }

  // TTL of -1 means never expires
  if (ttlSeconds < 0) {
    return true;
  }

  qint64 ageSeconds = info.lastModified().secsTo(QDateTime::currentDateTime());
  return ageSeconds < ttlSeconds;
}

QJsonDocument APICache::read(const QString &profileId,
                             const QString &endpoint) const {
  QString path = cachePath(profileId, endpoint);
  QFile file(path);

  if (!file.open(QIODevice::ReadOnly)) {
    return QJsonDocument();
  }

  QByteArray data = file.readAll();
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "APICache: Parse error for" << path << ":"
               << parseError.errorString();
    return QJsonDocument();
  }

  return doc;
}

bool APICache::write(const QString &profileId, const QString &endpoint,
                     const QJsonDocument &data) {
  QString path = cachePath(profileId, endpoint);

  // Ensure profile cache directory exists
  QFileInfo info(path);
  QDir().mkpath(info.absolutePath());

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning() << "APICache: Failed to write" << path << ":"
               << file.errorString();
    return false;
  }

  file.write(data.toJson(QJsonDocument::Compact));
  return true;
}

void APICache::clearProfile(const QString &profileId) {
  QString profileDir = QDir(m_cacheDir).filePath(profileId);
  QDir dir(profileDir);

  if (dir.exists()) {
    dir.removeRecursively();
    qInfo() << "APICache: Cleared cache for profile:" << profileId;
  }
}

void APICache::clearEndpoint(const QString &profileId,
                             const QString &endpoint) {
  QString path = cachePath(profileId, endpoint);
  if (QFile::exists(path)) {
    QFile::remove(path);
  }
}

QString APICache::cachePath(const QString &profileId,
                            const QString &endpoint) const {
  // Sanitize endpoint: replace / with _ (e.g., "account/wallet" -> "account_wallet")
  QString sanitized = endpoint;
  sanitized.replace('/', '_');
  sanitized.replace('?', '_');
  sanitized.replace('&', '_');

  return QDir(m_cacheDir).filePath(profileId + "/" + sanitized + ".json");
}

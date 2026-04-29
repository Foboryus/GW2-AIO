/**
 * @file MarkerPackRegistry.cpp
 * @brief Online marker pack registry — download, version checking, ZIP
 * extraction
 *
 * 3-tier version checking:
 *   Tier 1: GitHub Releases API (12 packs) — GET
 * /repos/{owner}/{repo}/releases/latest Tier 2: HTTP HEAD (6 non-GitHub packs)
 * — Content-Length + Last-Modified Tier 3: SHA-256 hash comparison (fallback
 * for all)
 *
 * DO NOT ADD:
 * - UI code (belongs in MarkerPackBrowser)
 * - AtomicFileWriter (downloads are complete-or-fail, not profile data)
 */

#include "MarkerPackRegistry.h"
#include "core/AtomicFileWriter.h"
#include "core/ZipExtractor.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

// --- Constants ---
static constexpr int kVersionCheckTimeoutMs = 10000;  // 10s
static constexpr int kDownloadTimeoutMs = 120000;     // 2min for large packs
static constexpr int kManifestFetchTimeoutMs = 15000; // 15s for manifest
static const QString kVersionCacheFile = QStringLiteral("pack_versions.json");
static const QString kManifestCacheFile = QStringLiteral("manifest_cache.json");

// --- Constructor ---

MarkerPackRegistry::MarkerPackRegistry(const QString &packsPath,
                                       QObject *parent)
    : QObject(parent), m_packsPath(packsPath),
      m_network(new QNetworkAccessManager(this)) {
  loadVersionCache();
}

// --- Manifest ---

void MarkerPackRegistry::loadManifest() {
  // Step 1: Always load synchronously first (cache → bundled)
  // This ensures packs are available immediately for the UI.
  if (!loadCachedManifest()) {
    loadBundledManifest();
  }
  refreshInstallStatus();
  emit manifestLoaded();

  // Step 2: If cooldown allows, fire async remote fetch in background.
  // If newer data is found, packs update and manifestLoaded re-emits.
  if (canCheckManifest()) {
    QNetworkRequest request(QUrl(QString::fromLatin1(kManifestUrl)));
    request.setTransferTimeout(kManifestFetchTimeoutMs);
    request.setRawHeader("User-Agent", "GW2AIO/1.0");

    // Conditional request: if we have an ETag, send If-None-Match
    if (!m_manifestETag.isEmpty()) {
      request.setRawHeader("If-None-Match", m_manifestETag.toUtf8());
    }

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
      reply->deleteLater();

      if (reply->error() == QNetworkReply::NoError) {
        int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (httpStatus == 304) {
          // Not modified — already using correct data
          qInfo() << "MarkerPackRegistry: Remote manifest not modified (304)";
          m_lastManifestCheck = QDateTime::currentDateTimeUtc();
          saveVersionCache();
          return;
        }

        // 200 OK — new data available
        QByteArray data = reply->readAll();

        // Store ETag for future conditional requests
        QByteArray etag = reply->rawHeader("ETag");
        if (!etag.isEmpty()) {
          m_manifestETag = QString::fromUtf8(etag);
        }

        m_lastManifestCheck = QDateTime::currentDateTimeUtc();
        saveVersionCache();

        if (parseManifestJson(data)) {
          cacheManifest(data);
          qInfo() << "MarkerPackRegistry: Updated to remote manifest —"
                  << m_packs.size() << "packs";
          refreshInstallStatus();
          emit manifestLoaded(); // Re-emit so UI refreshes
        }
      } else {
        // Network error — silently use what we already loaded
        qWarning() << "MarkerPackRegistry: Remote fetch failed:"
                   << reply->errorString();
        emit manifestFetchFailed(reply->errorString());
      }
    });
  } else {
    qInfo() << "MarkerPackRegistry: Manifest cooldown active, skipping "
               "remote fetch";
  }
}

bool MarkerPackRegistry::fetchRemoteManifest() {
  if (!canCheckManifest()) {
    QDateTime nextCheck =
        m_lastManifestCheck.addSecs(kManifestCooldownHours * 3600);
    qint64 secsLeft = QDateTime::currentDateTimeUtc().secsTo(nextCheck);
    int hours = static_cast<int>(secsLeft / 3600);
    int minutes = static_cast<int>((secsLeft % 3600) / 60);
    qInfo() << "MarkerPackRegistry: Manifest check on cooldown." << hours << "h"
            << minutes << "m remaining";
    return false;
  }

  // Fire async remote fetch only — packs are already loaded from startup.
  // On success, manifestLoaded() re-emits so UI refreshes.
  QNetworkRequest request(QUrl(QString::fromLatin1(kManifestUrl)));
  request.setTransferTimeout(kManifestFetchTimeoutMs);
  request.setRawHeader("User-Agent", "GW2AIO/1.0");

  if (!m_manifestETag.isEmpty()) {
    request.setRawHeader("If-None-Match", m_manifestETag.toUtf8());
  }

  QNetworkReply *reply = m_network->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();

    if (reply->error() == QNetworkReply::NoError) {
      int httpStatus =
          reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

      if (httpStatus == 304) {
        qInfo() << "MarkerPackRegistry: Remote manifest not modified (304)";
        m_lastManifestCheck = QDateTime::currentDateTimeUtc();
        saveVersionCache();
        return;
      }

      // 200 OK — new data available
      QByteArray data = reply->readAll();

      QByteArray etag = reply->rawHeader("ETag");
      if (!etag.isEmpty()) {
        m_manifestETag = QString::fromUtf8(etag);
      }

      m_lastManifestCheck = QDateTime::currentDateTimeUtc();
      saveVersionCache();

      if (parseManifestJson(data)) {
        cacheManifest(data);
        qInfo() << "MarkerPackRegistry: Updated to remote manifest —"
                << m_packs.size() << "packs";
        refreshInstallStatus();
        emit manifestLoaded();
      }
    } else {
      qWarning() << "MarkerPackRegistry: Remote fetch failed:"
                 << reply->errorString();
      emit manifestFetchFailed(reply->errorString());
    }
  });

  qInfo() << "MarkerPackRegistry: Fetching remote manifest (on-demand)";
  return true;
}

bool MarkerPackRegistry::canCheckManifest() const {
  if (!m_lastManifestCheck.isValid()) {
    return true; // Never checked
  }
  QDateTime nextAllowed =
      m_lastManifestCheck.addSecs(kManifestCooldownHours * 3600);
  return QDateTime::currentDateTimeUtc() >= nextAllowed;
}

bool MarkerPackRegistry::parseManifestJson(const QByteArray &data) {
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "MarkerPackRegistry: Manifest parse error:"
               << parseError.errorString();
    return false;
  }

  QJsonArray packsArray = doc.object().value("markerpacks").toArray();
  if (packsArray.isEmpty()) {
    qWarning() << "MarkerPackRegistry: Manifest has no packs";
    return false;
  }

  m_packs.clear();
  m_packs.reserve(packsArray.size());

  for (const QJsonValue &val : packsArray) {
    QJsonObject obj = val.toObject();
    OnlineMarkerPack pack;
    pack.id = obj.value("id").toString();
    pack.name = obj.value("name").toString();
    pack.description = obj.value("description").toString();
    pack.filename = obj.value("filename").toString();
    pack.downloadUrl = obj.value("downloadurl").toString();
    pack.backupUrl = obj.value("backupurl").toString();
    pack.enabledByDefault = obj.value("enabledbydefault").toBool(false);
    pack.autoDownloadOnInstall =
        obj.value("autoDownloadOnInstall").toBool(false);
    pack.author = obj.value("author").toString();
    pack.sourceUrl = obj.value("sourceUrl").toString();

    // Determine version strategy from manifest or infer from URL
    QString strategy = obj.value("versionStrategy").toString();
    if (strategy == "github") {
      pack.versionStrategy = OnlineMarkerPack::GitHubReleases;
      pack.githubOwner = obj.value("githubOwner").toString();
      pack.githubRepo = obj.value("githubRepo").toString();
    } else if (strategy == "httphead") {
      pack.versionStrategy = OnlineMarkerPack::HttpHead;
    } else {
      // Auto-detect: if downloadUrl contains github.com, use GitHub strategy
      if (pack.downloadUrl.contains("github.com")) {
        pack.versionStrategy = OnlineMarkerPack::GitHubReleases;
        // Extract owner/repo from URL:
        // https://github.com/OWNER/REPO/...
        QRegularExpression re(R"(github\.com/([^/]+)/([^/]+))");
        auto match = re.match(pack.downloadUrl);
        if (match.hasMatch()) {
          pack.githubOwner = match.captured(1);
          pack.githubRepo = match.captured(2);
        }
      } else {
        pack.versionStrategy = OnlineMarkerPack::HttpHead;
      }
    }

    // Restore version info from cache
    if (m_versionCache.contains(pack.id)) {
      QJsonObject cached = m_versionCache.value(pack.id).toObject();
      pack.installedVersion = cached.value("installedVersion").toString();
    }

    m_packs.append(pack);
  }

  return true;
}

bool MarkerPackRegistry::loadCachedManifest() {
  QString cachePath = QDir(m_packsPath).filePath(kManifestCacheFile);
  QFile file(cachePath);
  if (!file.open(QIODevice::ReadOnly)) {
    qInfo() << "MarkerPackRegistry: No cached manifest found";
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  // Validate the cache wrapper
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "MarkerPackRegistry: Cached manifest corrupt";
    return false;
  }

  QJsonObject root = doc.object();
  if (root.value("type").toString() != "markerPackManifestCache") {
    qWarning() << "MarkerPackRegistry: Cached manifest wrong type";
    return false;
  }

  // Extract the raw manifest payload
  QByteArray manifestData = QJsonDocument(root.value("manifest").toObject())
                                .toJson(QJsonDocument::Compact);

  if (parseManifestJson(manifestData)) {
    qInfo() << "MarkerPackRegistry: Loaded" << m_packs.size()
            << "packs from cached manifest";
    return true;
  }
  return false;
}

void MarkerPackRegistry::cacheManifest(const QByteArray &data) {
  // Wrap raw manifest data in a versioned envelope
  QJsonParseError parseError;
  QJsonDocument manifestDoc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    return; // Don't cache invalid data
  }

  QJsonObject root;
  root["type"] = QStringLiteral("markerPackManifestCache");
  root["version"] = 1;
  root["cachedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  root["manifest"] = manifestDoc.object();

  QString cachePath = QDir(m_packsPath).filePath(kManifestCacheFile);
  AtomicFileWriter::writeJson(cachePath, root);
  qInfo() << "MarkerPackRegistry: Cached manifest to" << cachePath;
}

void MarkerPackRegistry::loadBundledManifest() {
  QFile manifestFile(QStringLiteral(":/data/marker_packs.json"));
  if (!manifestFile.open(QIODevice::ReadOnly)) {
    qWarning() << "MarkerPackRegistry: Failed to open bundled manifest";
    return;
  }

  QByteArray data = manifestFile.readAll();
  manifestFile.close();

  if (parseManifestJson(data)) {
    qInfo() << "MarkerPackRegistry: Loaded" << m_packs.size()
            << "packs from bundled manifest (fallback)";
  } else {
    qWarning() << "MarkerPackRegistry: Bundled manifest parse failed!";
  }
}

const QList<OnlineMarkerPack> &MarkerPackRegistry::packs() const {
  return m_packs;
}

OnlineMarkerPack *MarkerPackRegistry::packById(const QString &id) {
  for (int i = 0; i < m_packs.size(); ++i) {
    if (m_packs[i].id == id) {
      return &m_packs[i];
    }
  }
  return nullptr;
}

// --- Install Status ---

void MarkerPackRegistry::refreshInstallStatus() {
  QDir dir(m_packsPath);
  QStringList installedFiles = dir.entryList(
      QStringList() << "*.taco" << "*.zip" << "*.aio", QDir::Files);

  // Validate installed files — remove corrupt archives (e.g., HTML error pages)
  QStringList validFiles;
  for (const QString &file : installedFiles) {
    QString filePath = dir.filePath(file);
    if (!ZipExtractor::isValidZipFile(filePath)) {
      qWarning() << "MarkerPackRegistry: Removing corrupt archive:" << file;
      QFile::remove(filePath);
      // Also clean up any cached extraction directory
      if (!m_cacheDir.isEmpty()) {
        QFileInfo fi(file);
        QString cacheSubDir =
            QDir(m_cacheDir).filePath(fi.completeBaseName());
        if (QDir(cacheSubDir).exists()) {
          QDir(cacheSubDir).removeRecursively();
        }
      }
    } else {
      validFiles.append(file);
    }
  }

  for (int i = 0; i < m_packs.size(); ++i) {
    OnlineMarkerPack &pack = m_packs[i];
    if (pack.status == OnlineMarkerPack::Downloading) {
      continue; // Don't touch active downloads
    }

    bool found = false;
    for (const QString &file : validFiles) {
      if (file.compare(pack.filename, Qt::CaseInsensitive) == 0) {
        found = true;
        break;
      }
    }

    OnlineMarkerPack::Status oldStatus = pack.status;
    if (found) {
      // Keep UpdateAvailable if we already detected it
      if (pack.status != OnlineMarkerPack::UpdateAvailable) {
        pack.status = OnlineMarkerPack::Installed;
      }
    } else {
      pack.status = OnlineMarkerPack::NotInstalled;
    }

    if (pack.status != oldStatus) {
      emit packStatusChanged(pack.id);
    }
  }
}

// --- Version Checking ---

void MarkerPackRegistry::checkForUpdates() {
  m_pendingVersionChecks = 0;

  for (int i = 0; i < m_packs.size(); ++i) {
    OnlineMarkerPack &pack = m_packs[i];
    // Only check installed packs
    if (pack.status != OnlineMarkerPack::Installed &&
        pack.status != OnlineMarkerPack::UpdateAvailable) {
      continue;
    }

    m_pendingVersionChecks++;

    switch (pack.versionStrategy) {
    case OnlineMarkerPack::GitHubReleases:
      checkGitHubVersion(pack);
      break;
    case OnlineMarkerPack::HttpHead:
      checkHttpHeadVersion(pack);
      break;
    case OnlineMarkerPack::None:
      m_pendingVersionChecks--;
      break;
    }
  }

  if (m_pendingVersionChecks == 0) {
    emit updateCheckComplete();
  }
}

void MarkerPackRegistry::checkGitHubVersion(OnlineMarkerPack &pack) {
  // GitHub API: GET /repos/{owner}/{repo}/releases/latest
  QString apiUrl =
      QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
          .arg(pack.githubOwner, pack.githubRepo);

  QUrl apiQUrl(apiUrl);
  QNetworkRequest request{apiQUrl};
  request.setRawHeader("Accept", "application/vnd.github.v3+json");
  request.setRawHeader("User-Agent", "GW2AIO/1.0");
  request.setTransferTimeout(kVersionCheckTimeoutMs);

  QNetworkReply *reply = m_network->get(request);
  QString packId = pack.id;

  connect(reply, &QNetworkReply::finished, this, [this, reply, packId]() {
    reply->deleteLater();
    OnlineMarkerPack *pack = packById(packId);
    if (!pack) {
      return;
    }

    if (reply->error() == QNetworkReply::NoError) {
      QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
      QString remoteTag = doc.object().value("tag_name").toString();

      if (!remoteTag.isEmpty()) {
        pack->remoteVersion = remoteTag;

        // Compare with installed version
        if (!pack->installedVersion.isEmpty() &&
            pack->installedVersion != remoteTag) {
          pack->status = OnlineMarkerPack::UpdateAvailable;
          emit packStatusChanged(pack->id);
        }

        // Update cache
        QJsonObject cached = m_versionCache.value(pack->id).toObject();
        cached["remoteVersion"] = remoteTag;
        cached["lastChecked"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        m_versionCache[pack->id] = cached;
      }
    } else {
      qDebug() << "MarkerPackRegistry: GitHub version check failed for"
               << packId << ":" << reply->errorString();
      // Tier 1 failed — fall through to Tier 2 (HTTP HEAD)
      checkHttpHeadVersion(*pack);
      return; // Don't decrement yet, Tier 2 will handle it
    }

    m_pendingVersionChecks--;
    if (m_pendingVersionChecks <= 0) {
      saveVersionCache();
      emit updateCheckComplete();
    }
  });
}

void MarkerPackRegistry::checkHttpHeadVersion(OnlineMarkerPack &pack) {
  QUrl headQUrl(pack.downloadUrl);
  QNetworkRequest request{headQUrl};
  request.setRawHeader("User-Agent", "GW2AIO/1.0");
  request.setTransferTimeout(kVersionCheckTimeoutMs);

  QNetworkReply *reply = m_network->head(request);
  QString packId = pack.id;

  connect(reply, &QNetworkReply::finished, this, [this, reply, packId]() {
    reply->deleteLater();
    OnlineMarkerPack *pack = packById(packId);
    if (!pack) {
      return;
    }

    if (reply->error() == QNetworkReply::NoError) {
      qint64 remoteSize =
          reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
      QString lastModified =
          reply->header(QNetworkRequest::LastModifiedHeader).toString();

      QJsonObject cached = m_versionCache.value(pack->id).toObject();
      qint64 cachedSize = cached.value("fileSize").toInteger(0);
      QString cachedModified = cached.value("lastModified").toString();

      bool changed = false;
      if (remoteSize > 0 && cachedSize > 0 && remoteSize != cachedSize) {
        changed = true;
      }
      if (!lastModified.isEmpty() && !cachedModified.isEmpty() &&
          lastModified != cachedModified) {
        changed = true;
      }

      if (changed && pack->status == OnlineMarkerPack::Installed) {
        pack->status = OnlineMarkerPack::UpdateAvailable;
        emit packStatusChanged(pack->id);
      }

      // Update cache
      if (remoteSize > 0) {
        cached["remoteFileSize"] = remoteSize;
      }
      if (!lastModified.isEmpty()) {
        cached["remoteLastModified"] = lastModified;
      }
      cached["lastChecked"] =
          QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
      m_versionCache[pack->id] = cached;
    } else {
      qDebug() << "MarkerPackRegistry: HTTP HEAD check failed for" << packId
               << ":" << reply->errorString();
      // Tier 2 also failed — graceful degradation, keep current status
    }

    m_pendingVersionChecks--;
    if (m_pendingVersionChecks <= 0) {
      saveVersionCache();
      emit updateCheckComplete();
    }
  });
}

// --- Download ---

void MarkerPackRegistry::downloadPack(const QString &packId) {
  OnlineMarkerPack *pack = packById(packId);
  if (!pack) {
    qWarning() << "MarkerPackRegistry: Unknown pack ID:" << packId;
    return;
  }

  if (m_activeDownloads.contains(packId)) {
    qDebug() << "MarkerPackRegistry: Download already in progress for"
             << packId;
    return;
  }

  pack->status = OnlineMarkerPack::Downloading;
  pack->downloadProgress = 0;
  pack->errorMessage.clear();
  emit packStatusChanged(packId);

  startDownload(*pack, pack->downloadUrl);
}

void MarkerPackRegistry::startDownload(OnlineMarkerPack &pack,
                                       const QString &url) {
  QUrl dlQUrl(url);
  QNetworkRequest request{dlQUrl};
  request.setRawHeader("User-Agent", "GW2AIO/1.0");
  request.setTransferTimeout(kDownloadTimeoutMs);

  QNetworkReply *reply = m_network->get(request);
  QString packId = pack.id;
  m_activeDownloads.insert(packId, reply);

  // Progress tracking
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this, packId](qint64 received, qint64 total) {
            OnlineMarkerPack *pack = packById(packId);
            if (!pack || total <= 0) {
              return;
            }
            int percent = static_cast<int>((received * 100) / total);
            pack->downloadProgress = percent;
            emit downloadProgress(packId, percent);
          });

  // Completion
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, packId]() { handleDownloadComplete(packId, reply); });
}

void MarkerPackRegistry::handleDownloadComplete(const QString &packId,
                                                QNetworkReply *reply) {
  reply->deleteLater();
  m_activeDownloads.remove(packId);

  OnlineMarkerPack *pack = packById(packId);
  if (!pack) {
    return;
  }

  // Check for network error
  if (reply->error() != QNetworkReply::NoError) {
    // Try backup URL if available and this was the primary
    if (!pack->backupUrl.isEmpty() &&
        reply->url().toString() != pack->backupUrl) {
      qInfo() << "MarkerPackRegistry: Primary download failed for" << packId
              << ", trying backup URL";
      startDownload(*pack, pack->backupUrl);
      return;
    }

    pack->status = OnlineMarkerPack::Error;
    pack->errorMessage = reply->errorString();
    emit packStatusChanged(packId);
    emit downloadFinished(packId, false, pack->errorMessage);
    return;
  }

  QByteArray data = reply->readAll();
  if (data.isEmpty()) {
    pack->status = OnlineMarkerPack::Error;
    pack->errorMessage = QStringLiteral("Downloaded file is empty");
    emit packStatusChanged(packId);
    emit downloadFinished(packId, false, pack->errorMessage);
    return;
  }

  // Validate ZIP magic bytes (PK = 0x50 0x4B)
  // Catches HTML error pages, 404s, CAPTCHAs saved as .taco files
  if (!ZipExtractor::isValidZipData(data)) {
    qWarning() << "MarkerPackRegistry: Download is not a valid ZIP archive"
               << "for" << pack->name
               << "(got" << data.left(20) << ")";

    // Try backup URL before failing
    if (!pack->backupUrl.isEmpty() &&
        reply->url().toString() != pack->backupUrl) {
      qInfo() << "MarkerPackRegistry: Trying backup URL for" << packId;
      startDownload(*pack, pack->backupUrl);
      return;
    }

    pack->status = OnlineMarkerPack::Error;
    pack->errorMessage =
        QStringLiteral("Downloaded file is not a valid ZIP/TACO archive");
    emit packStatusChanged(packId);
    emit downloadFinished(packId, false, pack->errorMessage);
    return;
  }

  // Compute SHA-256 for version cache
  QByteArray hash =
      QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();

  // Determine if this is a ZIP that needs extraction
  bool isZipNotTaco = pack->downloadUrl.endsWith(".zip", Qt::CaseInsensitive) ||
                      pack->backupUrl.endsWith(".zip", Qt::CaseInsensitive);

  QString finalPath;

  if (isZipNotTaco) {
    // Save to temp, then extract .taco from inside
    QString tempZipPath =
        QDir(m_packsPath).filePath(pack->id + QStringLiteral("_temp.zip"));

    QFile tempFile(tempZipPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
      pack->status = OnlineMarkerPack::Error;
      pack->errorMessage = QStringLiteral("Failed to write temp file");
      emit packStatusChanged(packId);
      emit downloadFinished(packId, false, pack->errorMessage);
      return;
    }
    tempFile.write(data);
    tempFile.close();

    if (!extractTacoFromZip(tempZipPath, pack->filename)) {
      pack->status = OnlineMarkerPack::Error;
      pack->errorMessage =
          QStringLiteral("Failed to extract .taco from .zip: ") +
          ZipExtractor::lastError();
      emit packStatusChanged(packId);
      emit downloadFinished(packId, false, pack->errorMessage);
      QFile::remove(tempZipPath);
      return;
    }

    QFile::remove(tempZipPath);
    finalPath = QDir(m_packsPath).filePath(pack->filename);
  } else {
    // Direct .taco download — save to packsPath
    finalPath = QDir(m_packsPath).filePath(pack->filename);

    QFile outFile(finalPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
      pack->status = OnlineMarkerPack::Error;
      pack->errorMessage = QStringLiteral("Failed to write pack file");
      emit packStatusChanged(packId);
      emit downloadFinished(packId, false, pack->errorMessage);
      return;
    }
    outFile.write(data);
    outFile.close();
  }

  // Update version cache
  QJsonObject cached = m_versionCache.value(pack->id).toObject();
  cached["installedVersion"] = pack->remoteVersion.isEmpty()
                                   ? QStringLiteral("downloaded")
                                   : pack->remoteVersion;
  cached["sha256"] = QString::fromLatin1(hash);
  cached["fileSize"] = data.size();
  cached["downloadedAt"] =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  m_versionCache[pack->id] = cached;
  saveVersionCache();

  // Update pack state
  pack->installedVersion = cached.value("installedVersion").toString();
  pack->status = OnlineMarkerPack::Installed;
  pack->downloadProgress = 100;
  emit packStatusChanged(packId);
  emit downloadFinished(packId, true, QString());

  qInfo() << "MarkerPackRegistry: Successfully downloaded" << pack->name << "to"
          << finalPath;
}

void MarkerPackRegistry::cancelDownload(const QString &packId) {
  if (!m_activeDownloads.contains(packId)) {
    return;
  }

  QNetworkReply *reply = m_activeDownloads.take(packId);
  reply->abort();
  reply->deleteLater();

  OnlineMarkerPack *pack = packById(packId);
  if (pack) {
    pack->status = OnlineMarkerPack::NotInstalled;
    pack->downloadProgress = 0;
    emit packStatusChanged(packId);
  }

  qInfo() << "MarkerPackRegistry: Cancelled download for" << packId;
}

// --- ZIP Extraction ---

bool MarkerPackRegistry::extractTacoFromZip(const QString &zipPath,
                                            const QString &expectedName) {
  QStringList files = ZipExtractor::listFiles(zipPath);
  if (files.isEmpty()) {
    qWarning() << "MarkerPackRegistry: ZIP is empty or unreadable:" << zipPath;
    return false;
  }

  // Strategy 1: Look for the expected filename at root
  for (const QString &file : files) {
    if (file.compare(expectedName, Qt::CaseInsensitive) == 0) {
      QByteArray data = ZipExtractor::extractFile(zipPath, file);
      if (!data.isEmpty()) {
        QString outPath = QDir(m_packsPath).filePath(expectedName);
        QFile out(outPath);
        if (out.open(QIODevice::WriteOnly)) {
          out.write(data);
          out.close();
          qInfo() << "MarkerPackRegistry: Extracted" << expectedName
                  << "from ZIP";
          return true;
        }
      }
    }
  }

  // Strategy 2: Find any .taco file (might be nested in subdirectory)
  QString bestTaco;
  qint64 bestSize = 0;
  for (const QString &file : files) {
    if (file.endsWith(".taco", Qt::CaseInsensitive) && !file.endsWith('/')) {
      QByteArray data = ZipExtractor::extractFile(zipPath, file);
      if (data.size() > bestSize) {
        bestTaco = file;
        bestSize = data.size();
      }
    }
  }

  if (!bestTaco.isEmpty()) {
    QByteArray data = ZipExtractor::extractFile(zipPath, bestTaco);
    if (!data.isEmpty()) {
      // Use expectedName as the output filename for consistency
      QString outPath = QDir(m_packsPath).filePath(expectedName);
      QFile out(outPath);
      if (out.open(QIODevice::WriteOnly)) {
        out.write(data);
        out.close();
        qInfo() << "MarkerPackRegistry: Extracted .taco" << bestTaco
                << "from ZIP as" << expectedName;
        return true;
      }
    }
  }

  // Strategy 3: No .taco found — rename the .zip itself to .taco
  // (Both are ZIP archives — TacO/AIO opens either)
  qInfo() << "MarkerPackRegistry: No .taco in ZIP, renaming to" << expectedName;
  qInfo() << "  ZIP contents:" << files;
  QString outPath = QDir(m_packsPath).filePath(expectedName);
  // Remove existing file first — QFile::copy fails if destination exists
  if (QFile::exists(outPath)) {
    QFile::remove(outPath);
  }
  if (QFile::copy(zipPath, outPath)) {
    qInfo() << "MarkerPackRegistry: Renamed ZIP to" << expectedName;
    return true;
  }
  qWarning() << "MarkerPackRegistry: Failed to copy ZIP to" << outPath;
  return false;
}

// --- Delete ---

bool MarkerPackRegistry::deletePack(const QString &packId) {
  OnlineMarkerPack *pack = packById(packId);
  if (!pack) {
    qWarning() << "MarkerPackRegistry: Unknown pack ID for delete:" << packId;
    return false;
  }

  // Remove the .taco file
  QString filePath = QDir(m_packsPath).filePath(pack->filename);
  if (QFile::exists(filePath)) {
    if (!QFile::remove(filePath)) {
      qWarning() << "MarkerPackRegistry: Failed to delete" << filePath;
      return false;
    }
    qInfo() << "MarkerPackRegistry: Deleted" << filePath;
  }

  // Remove extracted cache directory (e.g., MarkerPacksCache/MyPack/)
  if (!m_cacheDir.isEmpty()) {
    QFileInfo fi(pack->filename);
    QString cacheSubDir = QDir(m_cacheDir).filePath(fi.completeBaseName());
    if (QDir(cacheSubDir).exists()) {
      qInfo() << "MarkerPackRegistry: Removing cache dir:"
              << fi.completeBaseName();
      QDir(cacheSubDir).removeRecursively();
    }
  }

  // Remove from version cache
  m_versionCache.remove(packId);
  saveVersionCache();

  // Update state
  pack->status = OnlineMarkerPack::NotInstalled;
  pack->installedVersion.clear();
  pack->downloadProgress = 0;
  emit packStatusChanged(packId);

  return true;
}

// --- Auto-download on first install ---

void MarkerPackRegistry::autoDownloadFirstLaunchPacks() {
  if (!m_isFirstLaunch) {
    return;
  }

  qInfo() << "MarkerPackRegistry: First launch — auto-downloading essential "
             "packs";

  for (const OnlineMarkerPack &pack : m_packs) {
    if (pack.autoDownloadOnInstall &&
        pack.status == OnlineMarkerPack::NotInstalled) {
      qInfo() << "MarkerPackRegistry: Auto-downloading" << pack.name;
      downloadPack(pack.id);
    }
  }

  // Clear first-launch flag so this doesn't repeat
  m_isFirstLaunch = false;
}

// --- Version Cache ---

void MarkerPackRegistry::loadVersionCache() {
  QString cachePath = QDir(m_packsPath).filePath(kVersionCacheFile);
  QFile file(cachePath);
  if (!file.open(QIODevice::ReadOnly)) {
    m_isFirstLaunch = true; // No cache file → first launch
    return;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "MarkerPackRegistry: Version cache parse error:"
               << parseError.errorString();
    return;
  }

  QJsonObject root = doc.object();
  if (root.value("type").toString() != "markerPackVersions") {
    qWarning() << "MarkerPackRegistry: Invalid version cache type";
    return;
  }

  m_versionCache = root.value("packs").toObject();

  // Restore manifest check state
  QString lastCheck = root.value("lastManifestCheck").toString();
  if (!lastCheck.isEmpty()) {
    m_lastManifestCheck = QDateTime::fromString(lastCheck, Qt::ISODate);
    m_lastManifestCheck.setTimeSpec(Qt::UTC);
  }
  m_manifestETag = root.value("manifestETag").toString();

  qInfo() << "MarkerPackRegistry: Loaded version cache with"
          << m_versionCache.size() << "entries";
}

void MarkerPackRegistry::saveVersionCache() {
  QJsonObject root;
  root["type"] = QStringLiteral("markerPackVersions");
  root["version"] = 1;
  root["packs"] = m_versionCache;

  // Persist manifest check state
  if (m_lastManifestCheck.isValid()) {
    root["lastManifestCheck"] = m_lastManifestCheck.toString(Qt::ISODate);
  }
  if (!m_manifestETag.isEmpty()) {
    root["manifestETag"] = m_manifestETag;
  }

  QString cachePath = QDir(m_packsPath).filePath(kVersionCacheFile);
  QFile file(cachePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning() << "MarkerPackRegistry: Failed to save version cache";
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.close();
}

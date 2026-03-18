#include "UpdateChecker.h"
#include <QNetworkRequest>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

void UpdateChecker::checkForUpdate(const InstalledAddon& addon)
{
    QString sourceUrl = addon.definition.sourceUrl;
    
    if (addon.definition.id == "arcdps") {
        checkArcDPS(addon.definition.id);
    } else if (sourceUrl.contains("github.com")) {
        QString repo = extractGitHubRepo(sourceUrl);
        if (!repo.isEmpty()) {
            checkGitHubRelease(repo, addon.definition.id);
        }
    }
}

void UpdateChecker::checkAllUpdates(const QList<InstalledAddon>& addons)
{
    for (const InstalledAddon& addon : addons) {
        checkForUpdate(addon);
    }
}

void UpdateChecker::downloadUpdate(const UpdateInfo& info)
{
    QNetworkRequest request(QUrl(info.downloadUrl));
    request.setHeader(QNetworkRequest::UserAgentHeader, "GW2AIO/0.1");
    
    QNetworkReply* reply = m_networkManager->get(request);
    m_pendingDownloads[reply] = info.addonId;
    
    connect(reply, &QNetworkReply::downloadProgress, 
            this, &UpdateChecker::onDownloadProgress);
    connect(reply, &QNetworkReply::finished, 
            this, [this, reply]() { onDownloadFinished(reply); });
}

void UpdateChecker::onGitHubReplyFinished(QNetworkReply* reply)
{
    QString addonId = m_pendingChecks.take(reply);
    reply->deleteLater();
    
    if (reply->error() != QNetworkReply::NoError) {
        emit error(addonId, reply->errorString());
        return;
    }
    
    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject release = doc.object();
    
    UpdateInfo info;
    info.addonId = addonId;
    info.latestVersion = release["tag_name"].toString();
    info.releaseNotes = release["body"].toString();
    
    // Find download URL from assets
    QJsonArray assets = release["assets"].toArray();
    for (const QJsonValue& asset : assets) {
        QJsonObject obj = asset.toObject();
        QString name = obj["name"].toString().toLower();
        if (name.endsWith(".zip") || name.endsWith(".dll")) {
            info.downloadUrl = obj["browser_download_url"].toString();
            break;
        }
    }
    
    info.updateAvailable = !info.latestVersion.isEmpty();
    emit updateCheckComplete(info);
}

void UpdateChecker::onDownloadProgress(qint64 received, qint64 total)
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply && m_pendingDownloads.contains(reply)) {
        emit downloadProgress(m_pendingDownloads[reply], received, total);
    }
}

void UpdateChecker::onDownloadFinished(QNetworkReply* reply)
{
    QString addonId = m_pendingDownloads.take(reply);
    reply->deleteLater();
    
    if (reply->error() != QNetworkReply::NoError) {
        emit error(addonId, reply->errorString());
        return;
    }
    
    // Save to temp directory
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString filePath = QDir(tempDir).filePath("gw2aio_" + addonId + ".download");
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reply->readAll());
        file.close();
        emit downloadComplete(addonId, filePath);
    } else {
        emit error(addonId, "Failed to save download: " + file.errorString());
    }
}

QString UpdateChecker::extractGitHubRepo(const QString& url) const
{
    // Extract "owner/repo" from GitHub URL
    QRegularExpression rx("github\\.com[/:]([^/]+)/([^/]+)");
    auto match = rx.match(url);
    if (match.hasMatch()) {
        QString owner = match.captured(1);
        QString repo = match.captured(2);
        if (repo.endsWith(".git")) {
            repo.chop(4);
        }
        return owner + "/" + repo;
    }
    return QString();
}

void UpdateChecker::checkGitHubRelease(const QString& repo, const QString& addonId)
{
    QString urlStr = QString("https://api.github.com/repos/%1/releases/latest").arg(repo);
    QUrl url{urlStr};
    
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, "GW2AIO/0.1");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    
    QNetworkReply* reply = m_networkManager->get(request);
    m_pendingChecks[reply] = addonId;
    
    connect(reply, &QNetworkReply::finished, 
            this, [this, reply]() { onGitHubReplyFinished(reply); });
}

void UpdateChecker::checkArcDPS(const QString& addonId)
{
    // ArcDPS uses a simple HEAD request to check for updates
    // The file is always at the same URL, version embedded in binary
    UpdateInfo info;
    info.addonId = addonId;
    info.latestVersion = "Latest";  // ArcDPS doesn't use version numbers
    info.downloadUrl = "https://deltaconnected.com/arcdps/x64/d3d11.dll";
    info.updateAvailable = true;    // Always offer to re-download
    
    emit updateCheckComplete(info);
}

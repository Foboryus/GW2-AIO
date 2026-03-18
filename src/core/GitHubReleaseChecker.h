// REVIEW BEFORE BETA: all inline (179 lines) — split to .h/.cpp pair.
#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVersionNumber>
#include <QRegularExpression>

/**
 * @brief Monitors GitHub releases for updates
 */
class GitHubReleaseChecker : public QObject
{
    Q_OBJECT
    
public:
    struct Release {
        QString tagName;
        QString name;
        QString body;           // Release notes (markdown)
        QString htmlUrl;
        QString publishedAt;
        bool prerelease = false;
        bool draft = false;
        
        struct Asset {
            QString name;
            QString downloadUrl;
            QString contentType;
            qint64 size;
        };
        QList<Asset> assets;
        
        QVersionNumber version() const {
            QString v = tagName;
            v.remove(QRegularExpression("^[vV]"));
            return QVersionNumber::fromString(v);
        }
    };
    
    explicit GitHubReleaseChecker(QObject* parent = nullptr);
    
    /**
     * @brief Check for latest release
     * @param owner Repository owner
     * @param repo Repository name
     */
    void checkLatestRelease(const QString& owner, const QString& repo);
    
    /**
     * @brief Check all releases
     */
    void checkAllReleases(const QString& owner, const QString& repo);
    
    /**
     * @brief Get last checked release
     */
    const Release& latestRelease() const { return m_latestRelease; }
    
    /**
     * @brief Check if update is available
     */
    bool isUpdateAvailable(const QVersionNumber& currentVersion) const;
    
signals:
    void releaseFound(const Release& release);
    void releasesFound(const QList<Release>& releases);
    void checkFailed(const QString& error);
    void updateAvailable(const Release& release);
    
private slots:
    void onReplyFinished(QNetworkReply* reply);
    
private:
    Release parseRelease(const QJsonObject& json);
    
    QNetworkAccessManager* m_network;
    Release m_latestRelease;
    bool m_checkingAll = false;
};

// Implementation
inline GitHubReleaseChecker::GitHubReleaseChecker(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    connect(m_network, &QNetworkAccessManager::finished,
            this, &GitHubReleaseChecker::onReplyFinished);
}

inline void GitHubReleaseChecker::checkLatestRelease(const QString& owner, const QString& repo)
{
    m_checkingAll = false;
    QString url = QString("https://api.github.com/repos/%1/%2/releases/latest")
                      .arg(owner, repo);
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "GW2AIO/1.0");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    
    m_network->get(request);
}

inline void GitHubReleaseChecker::checkAllReleases(const QString& owner, const QString& repo)
{
    m_checkingAll = true;
    QString url = QString("https://api.github.com/repos/%1/%2/releases")
                      .arg(owner, repo);
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "GW2AIO/1.0");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    
    m_network->get(request);
}

inline bool GitHubReleaseChecker::isUpdateAvailable(const QVersionNumber& currentVersion) const
{
    return m_latestRelease.version() > currentVersion;
}

inline void GitHubReleaseChecker::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    
    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(reply->errorString());
        return;
    }
    
    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    
    if (m_checkingAll) {
        QList<Release> releases;
        QJsonArray arr = doc.array();
        for (const QJsonValue& val : arr) {
            releases.append(parseRelease(val.toObject()));
        }
        emit releasesFound(releases);
        
        if (!releases.isEmpty()) {
            m_latestRelease = releases.first();
        }
    } else {
        m_latestRelease = parseRelease(doc.object());
        emit releaseFound(m_latestRelease);
    }
}

inline GitHubReleaseChecker::Release GitHubReleaseChecker::parseRelease(const QJsonObject& json)
{
    Release r;
    r.tagName = json["tag_name"].toString();
    r.name = json["name"].toString();
    r.body = json["body"].toString();
    r.htmlUrl = json["html_url"].toString();
    r.publishedAt = json["published_at"].toString();
    r.prerelease = json["prerelease"].toBool();
    r.draft = json["draft"].toBool();
    
    QJsonArray assets = json["assets"].toArray();
    for (const QJsonValue& assetVal : assets) {
        QJsonObject assetObj = assetVal.toObject();
        Release::Asset asset;
        asset.name = assetObj["name"].toString();
        asset.downloadUrl = assetObj["browser_download_url"].toString();
        asset.contentType = assetObj["content_type"].toString();
        asset.size = assetObj["size"].toInteger();
        r.assets.append(asset);
    }
    
    return r;
}

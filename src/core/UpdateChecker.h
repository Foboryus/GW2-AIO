#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QMap>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "models/Addon.h"

/**
 * @brief Checks for addon updates from GitHub and other sources
 */
class UpdateChecker : public QObject
{
    Q_OBJECT
    
public:
    struct UpdateInfo
    {
        QString addonId;
        QString currentVersion;
        QString latestVersion;
        QString downloadUrl;
        QString releaseNotes;
        bool updateAvailable = false;
    };
    
    explicit UpdateChecker(QObject* parent = nullptr);
    
    /**
     * @brief Check for updates for a specific addon
     */
    void checkForUpdate(const InstalledAddon& addon);
    
    /**
     * @brief Check all installed addons for updates
     */
    void checkAllUpdates(const QList<InstalledAddon>& addons);
    
    /**
     * @brief Download addon update to temp file
     * @return Path to downloaded file
     */
    void downloadUpdate(const UpdateInfo& info);
    
signals:
    void updateCheckComplete(const UpdateInfo& info);
    void downloadProgress(const QString& addonId, qint64 received, qint64 total);
    void downloadComplete(const QString& addonId, const QString& filePath);
    void error(const QString& addonId, const QString& message);
    
private slots:
    void onGitHubReplyFinished(QNetworkReply* reply);
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished(QNetworkReply* reply);
    
private:
    QNetworkAccessManager* m_networkManager;
    QMap<QNetworkReply*, QString> m_pendingChecks;
    QMap<QNetworkReply*, QString> m_pendingDownloads;
    
    QString extractGitHubRepo(const QString& url) const;
    void checkGitHubRelease(const QString& repo, const QString& addonId);
    void checkArcDPS(const QString& addonId);
};

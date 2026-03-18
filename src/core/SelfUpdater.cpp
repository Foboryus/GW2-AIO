/**
 * @file SelfUpdater.cpp
 * @brief Self-update mechanism for the application
 *
 * DO NOT ADD:
 * - GitHub API logic (belongs in GitHubReleaseChecker)
 * - UI code
 */

#include "SelfUpdater.h"

#include <QDebug>

SelfUpdater::SelfUpdater(QObject *parent)
    : QObject(parent), m_checker(new GitHubReleaseChecker(this)),
      m_network(new QNetworkAccessManager(this)) {
  connect(m_checker, &GitHubReleaseChecker::releaseFound, this,
          &SelfUpdater::onReleaseFound);
  connect(m_checker, &GitHubReleaseChecker::checkFailed, this,
          &SelfUpdater::onCheckFailed);
}

void SelfUpdater::checkForUpdates() {
  setState(State::Checking);
  m_checker->checkLatestRelease(m_owner, m_repo);
}

void SelfUpdater::downloadUpdate() {
  if (!m_updateAvailable) {
    emit error("No update available");
    return;
  }

  const auto &release = m_checker->latestRelease();

  // Find the appropriate asset (exe or zip for Windows)
  QString downloadUrl;
  for (const auto &asset : release.assets) {
    if (asset.name.endsWith(".exe") || asset.name.endsWith(".zip")) {
      downloadUrl = asset.downloadUrl;
      m_downloadPath =
          QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
              .filePath(asset.name);
      break;
    }
  }

  if (downloadUrl.isEmpty()) {
    emit error("No compatible update package found");
    return;
  }

  setState(State::Downloading);

  QNetworkRequest request(downloadUrl);
  request.setHeader(QNetworkRequest::UserAgentHeader, "GW2AIO/1.0");

  QNetworkReply *reply = m_network->get(request);
  connect(reply, &QNetworkReply::downloadProgress, this,
          &SelfUpdater::onDownloadProgress);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { onDownloadFinished(reply); });
}

void SelfUpdater::installUpdate() {
  if (m_state != State::ReadyToInstall) {
    emit error("Update not ready to install");
    return;
  }

  setState(State::Installing);

  // Create updater script
  QString scriptPath =
      QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
          .filePath("gw2aio_update.bat");

  QString appPath = QCoreApplication::applicationFilePath();
  QString appDir = QCoreApplication::applicationDirPath();

  QFile script(scriptPath);
  if (script.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&script);
    out << "@echo off\n";
    out << "echo Updating GW2 AIO Manager...\n";
    out << "timeout /t 2 /nobreak > nul\n"; // Wait for app to close

    if (m_downloadPath.endsWith(".exe")) {
      // Direct exe replacement
      out << "copy /Y \"" << m_downloadPath << "\" \"" << appPath << "\"\n";
    } else if (m_downloadPath.endsWith(".zip")) {
      // Extract zip
      out << "powershell -Command \"Expand-Archive -Force '" << m_downloadPath
          << "' '" << appDir << "'\"\n";
    }

    out << "start \"\" \"" << appPath << "\"\n";
    out << "del \"%~f0\"\n"; // Delete self
    script.close();
  }

  // Start updater and quit
  QProcess::startDetached("cmd", {"/c", scriptPath});
  QCoreApplication::quit();
}

const GitHubReleaseChecker::Release &SelfUpdater::latestRelease() const {
  return m_checker->latestRelease();
}

QVersionNumber SelfUpdater::currentVersion() {
  return QVersionNumber::fromString(QCoreApplication::applicationVersion());
}

void SelfUpdater::onReleaseFound(const GitHubReleaseChecker::Release &release) {
  m_updateAvailable = release.version() > currentVersion();
  setState(State::Idle);
  emit updateCheckComplete(m_updateAvailable);

  if (m_updateAvailable) {
    qInfo() << "Update available:" << release.tagName;
  }
}

void SelfUpdater::onCheckFailed(const QString &err) {
  setState(State::Error);
  emit error(err);
}

void SelfUpdater::onDownloadProgress(qint64 received, qint64 total) {
  if (total > 0) {
    m_downloadProgress = static_cast<int>((received * 100) / total);
    emit downloadProgressChanged(m_downloadProgress);
  }
}

void SelfUpdater::onDownloadFinished(QNetworkReply *reply) {
  reply->deleteLater();

  if (reply->error() != QNetworkReply::NoError) {
    setState(State::Error);
    emit error(reply->errorString());
    return;
  }

  // Save file
  QFile file(m_downloadPath);
  if (!file.open(QIODevice::WriteOnly)) {
    setState(State::Error);
    emit error("Failed to save update file");
    return;
  }

  file.write(reply->readAll());
  file.close();

  setState(State::ReadyToInstall);
  emit updateReady();

  qInfo() << "Update downloaded to:" << m_downloadPath;
}

void SelfUpdater::setState(State state) {
  if (m_state != state) {
    m_state = state;
    emit stateChanged(state);
  }
}

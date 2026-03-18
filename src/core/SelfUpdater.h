#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>


#include "GitHubReleaseChecker.h"

/**
 * @brief Self-update mechanism for the application
 *
 * DO NOT ADD:
 * - Inline implementations (use SelfUpdater.cpp)
 */
class SelfUpdater : public QObject {
  Q_OBJECT

public:
  enum class State {
    Idle,
    Checking,
    Downloading,
    ReadyToInstall,
    Installing,
    Error
  };
  Q_ENUM(State)

  explicit SelfUpdater(QObject *parent = nullptr);

  /**
   * @brief Check for updates
   */
  void checkForUpdates();

  /**
   * @brief Download the update
   */
  void downloadUpdate();

  /**
   * @brief Install the update (restarts app)
   */
  void installUpdate();

  /**
   * @brief Get current state
   */
  State state() const { return m_state; }

  /**
   * @brief Get download progress (0-100)
   */
  int downloadProgress() const { return m_downloadProgress; }

  /**
   * @brief Check if update is available
   */
  bool updateAvailable() const { return m_updateAvailable; }

  /**
   * @brief Get latest release info
   */
  const GitHubReleaseChecker::Release &latestRelease() const;

  /**
   * @brief Get current app version
   */
  static QVersionNumber currentVersion();

signals:
  void stateChanged(State state);
  void updateCheckComplete(bool available);
  void downloadProgressChanged(int percent);
  void updateReady();
  void error(const QString &message);

private slots:
  void onReleaseFound(const GitHubReleaseChecker::Release &release);
  void onCheckFailed(const QString &error);
  void onDownloadProgress(qint64 received, qint64 total);
  void onDownloadFinished(QNetworkReply *reply);

private:
  void setState(State state);
  QString updateFilePath() const;

  GitHubReleaseChecker *m_checker;
  QNetworkAccessManager *m_network;

  State m_state = State::Idle;
  bool m_updateAvailable = false;
  int m_downloadProgress = 0;
  QString m_downloadPath;

  // GitHub repo info for this app
  QString m_owner = "Foboryus";
  QString m_repo = "GW2-AIO";
};

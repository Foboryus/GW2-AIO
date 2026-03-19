#pragma once

/**
 * @brief Smart Credential Refresh Manager
 *
 * Detects stale .dat files and refreshes them by launching
 * GW2 solo (no -shareArchive), waiting for LOADED signal, then
 * terminating. Only triggered by explicit user launch (pre-flight check).
 *
 * See: features/local-dat-management.md
 *
 * DO NOT ADD:
 * - Inline implementations (use CredentialRefreshManager.cpp)
 * - UI code (this is a manager, UI is in LauncherWidget)
 */

#include <QDateTime>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>

struct AccountProfile;
class LaunchManager;
class LocalDatManager;

class CredentialRefreshManager : public QObject {
  Q_OBJECT

public:
  explicit CredentialRefreshManager(LaunchManager *launchManager,
                                    LocalDatManager *localDatManager,
                                    QObject *parent = nullptr);

  /// Check which profiles have stale .dat files
  /// Only checks standalone profiles with autoLogin and a saved .dat
  QList<AccountProfile> getStaleProfiles(const QList<AccountProfile> &profiles,
                                         int thresholdHours = 24) const;

  /// Start sequential refresh of stale profiles
  /// Emits refreshProgress/refreshComplete/refreshFailed signals
  void refreshProfiles(const QList<AccountProfile> &profiles);

  /// Cancel ongoing refresh
  void cancel();

  bool isRefreshing() const { return m_refreshing; }

signals:
  void refreshProgress(int current, int total, const QString &profileName);
  void refreshComplete(int refreshedCount);
  void refreshFailed(const QString &profileName, const QString &reason);

private slots:
  void onProfileLaunched(const QString &profileId, qint64 pid);
  void onProfileLoaded(const QString &gw2Path);
  void onProcessExited(qint64 pid, int exitCode);
  void onTimeout();

private:
  void refreshNext();
  void finishRefresh();
  void terminateProcess(qint64 pid);

  LaunchManager *m_launchManager;
  LocalDatManager *m_localDatManager;

  QList<AccountProfile> m_queue;
  int m_currentIndex = -1;
  int m_refreshedCount = 0;
  bool m_refreshing = false;
  bool m_cancelled = false;


  // Current refresh state
  qint64 m_currentPid = 0;
  QString m_currentProfileId;
  bool m_waitingForLoaded = false;
  bool m_prevMultiBoxState = false;

  QTimer *m_timeoutTimer;

  static constexpr int kRefreshTimeoutMs = 90000; // 90s per profile
};

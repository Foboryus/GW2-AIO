#include "CredentialRefreshManager.h"

#include <QDebug>

#include "core/LaunchManager.h"
#include "core/LocalDatManager.h"
#include "core/ProfileManager.h" // AccountProfile

#ifdef Q_OS_WIN
#include <windows.h>
#endif

CredentialRefreshManager::CredentialRefreshManager(
    LaunchManager *launchManager, LocalDatManager *localDatManager,
    QObject *parent)
    : QObject(parent), m_launchManager(launchManager),
      m_localDatManager(localDatManager) {
  m_timeoutTimer = new QTimer(this);
  m_timeoutTimer->setSingleShot(true);
  connect(m_timeoutTimer, &QTimer::timeout, this,
          &CredentialRefreshManager::onTimeout);
}

QList<AccountProfile> CredentialRefreshManager::getStaleProfiles(
    const QList<AccountProfile> &profiles, int thresholdHours) const {
  QList<AccountProfile> stale;
  for (const auto &p : profiles) {
    // Only standalone profiles with a saved Local.dat need refresh.
    // Steam/Epic use platform auth — no .dat file involved.
    if (p.accountProvider != AccountProvider::Standalone)
      continue;
    if (p.localDatPath.isEmpty())
      continue;

    // Use AIO-tracked lastLoginTime (set on character select)
    // instead of filesystem timestamp for reliable staleness detection
    if (!p.lastLoginTime.isValid()) {
      qInfo() << "Stale credentials for" << p.nickname
              << "— never reached character select";
      stale.append(p);
      continue;
    }

    qint64 ageSecs = p.lastLoginTime.secsTo(QDateTime::currentDateTime());
    if (ageSecs > thresholdHours * 3600) {
      qInfo() << "Stale credentials for" << p.nickname
              << "— last character select:" << (ageSecs / 3600) << "hours ago";
      stale.append(p);
    }
  }
  return stale;
}

void CredentialRefreshManager::refreshProfiles(
    const QList<AccountProfile> &profiles) {
  if (m_refreshing || profiles.isEmpty()) {
    emit refreshComplete(0);
    return;
  }

  qInfo() << "Starting credential refresh for" << profiles.size() << "profiles";

  m_queue = profiles;
  m_currentIndex = -1;
  m_refreshedCount = 0;
  m_refreshing = true;
  m_cancelled = false;

  // Save multibox state for restore in finishRefresh()
  m_prevMultiBoxState = m_launchManager->multiBoxEnabled();

  // Disable multibox for solo refresh — no -shareArchive.
  // GW2 needs exclusive write access to Local.dat for credential update.
  // The caller (LauncherWidget) ensures no other GW2 instances are running.
  m_launchManager->setMultiBoxEnabled(false);

  // Connect signals for refresh tracking
  connect(m_launchManager, &LaunchManager::profileLaunched, this,
          &CredentialRefreshManager::onProfileLaunched);
  connect(m_launchManager, &LaunchManager::profileCharacterSelectReached,
          this, &CredentialRefreshManager::onProfileLoaded);
  connect(m_launchManager, &LaunchManager::gw2Exited, this,
          &CredentialRefreshManager::onProcessExited);

  // Junction approach: each profile folder is activated individually
  // No global backup needed before launching
  // Prevent LaunchManager from auto-deactivating junction mid-refresh
  m_launchManager->setExternalJunctionOwner(true);

  refreshNext();
}

void CredentialRefreshManager::refreshNext() {
  m_currentIndex++;

  if (m_cancelled || m_currentIndex >= m_queue.size()) {
    finishRefresh();
    return;
  }

  // Work on a copy — launchWithProfile may modify the profile
  AccountProfile profile = m_queue[m_currentIndex];
  m_currentProfileId = profile.id;
  m_currentPid = 0;
  m_waitingForLoaded = true;

  emit refreshProgress(m_currentIndex + 1, m_queue.size(), profile.nickname);
  qInfo() << "Refreshing credentials for:" << profile.nickname << "("
          << (m_currentIndex + 1) << "/" << m_queue.size() << ")";

  // Start timeout timer
  m_timeoutTimer->start(kRefreshTimeoutMs);

  // Launch profile solo (multibox disabled → no -shareArchive)
  // GW2 opens Local.dat with write access, updates cache/session data
  QProcess *proc = m_launchManager->launchWithProfile(profile);
  if (!proc && profile.accountProvider == AccountProvider::Standalone) {
    qWarning() << "Failed to launch for refresh:" << profile.nickname;
    emit refreshFailed(profile.nickname, "Failed to launch GW2");
    m_timeoutTimer->stop();
    // Continue to next profile
    QTimer::singleShot(0, this, &CredentialRefreshManager::refreshNext);
  }
}

void CredentialRefreshManager::onProfileLaunched(const QString &profileId,
                                                 qint64 pid) {
  if (!m_refreshing || profileId != m_currentProfileId)
    return;

  m_currentPid = pid;
  qInfo() << "Refresh: profile launched with PID:" << pid;
}

void CredentialRefreshManager::onProfileLoaded(const QString &profileId) {
  if (!m_refreshing || !m_waitingForLoaded || m_currentPid == 0)
    return;
  // Verify this is OUR profile's signal — not another running instance
  if (profileId != m_currentProfileId)
    return;

  qInfo() << "Refresh: character select reached for" << profileId
          << "PID:" << m_currentPid << "— .dat file updated, terminating GW2";

  m_waitingForLoaded = false;
  m_timeoutTimer->stop();
  m_refreshedCount++;

  // GW2 has updated the .dat file by now — safe to terminate
  // (GW2 does NOT write to Local.dat on exit, only during startup)
  m_launchManager->markDeliberatelyKilled(m_currentPid);
  terminateProcess(m_currentPid);
}

void CredentialRefreshManager::onProcessExited(qint64 pid, int exitCode) {
  Q_UNUSED(exitCode);
  if (!m_refreshing || pid != m_currentPid)
    return;

  qInfo() << "Refresh: GW2 exited for PID:" << pid << "— moving to next";

  // Brief delay before launching next profile
  QTimer::singleShot(1000, this, &CredentialRefreshManager::refreshNext);
}

void CredentialRefreshManager::onTimeout() {
  if (!m_refreshing)
    return;

  QString name = (m_currentIndex < m_queue.size())
                     ? m_queue[m_currentIndex].nickname
                     : QStringLiteral("Unknown");

  qWarning() << "Refresh timeout for:" << name << "after"
             << (kRefreshTimeoutMs / 1000) << "seconds";
  emit refreshFailed(name, "Timed out waiting for GW2 to load");

  m_waitingForLoaded = false;

  // Kill the hung process and move on
  if (m_currentPid > 0) {
    m_launchManager->markDeliberatelyKilled(m_currentPid);
    terminateProcess(m_currentPid);
    // onProcessExited will trigger refreshNext
  } else {
    refreshNext();
  }
}

void CredentialRefreshManager::cancel() {
  if (!m_refreshing)
    return;

  qInfo() << "Credential refresh cancelled by user";
  m_cancelled = true;
  m_timeoutTimer->stop();

  if (m_currentPid > 0) {
    m_launchManager->markDeliberatelyKilled(m_currentPid);
    terminateProcess(m_currentPid);
  }

  finishRefresh();
}

void CredentialRefreshManager::finishRefresh() {
  m_refreshing = false;
  m_waitingForLoaded = false;
  m_timeoutTimer->stop();

  // Disconnect signals
  disconnect(m_launchManager, &LaunchManager::profileLaunched, this,
             &CredentialRefreshManager::onProfileLaunched);
  disconnect(m_launchManager, &LaunchManager::profileCharacterSelectReached,
             this, &CredentialRefreshManager::onProfileLoaded);
  disconnect(m_launchManager, &LaunchManager::gw2Exited, this,
             &CredentialRefreshManager::onProcessExited);

  // Release junction ownership back to LaunchManager
  m_launchManager->setExternalJunctionOwner(false);

  // Deactivate junction to restore original AppData
  if (m_localDatManager->isJunctionActive()) {
    m_localDatManager->deactivateProfile();
  }

  // Restore multibox state
  m_launchManager->setMultiBoxEnabled(m_prevMultiBoxState);

  qInfo() << "Credential refresh finished:" << m_refreshedCount << "of"
          << m_queue.size() << "profiles refreshed";
  emit refreshComplete(m_refreshedCount);
}

void CredentialRefreshManager::terminateProcess(qint64 pid) {
#ifdef Q_OS_WIN
  HANDLE hProcess =
      OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (hProcess) {
    TerminateProcess(hProcess, 0);
    CloseHandle(hProcess);
    qInfo() << "Refresh: terminated GW2 process PID:" << pid;
  } else {
    qWarning() << "Refresh: failed to open process for termination, PID:" << pid
               << "error:" << GetLastError();
  }
#else
  Q_UNUSED(pid);
  qWarning() << "Refresh: process termination not implemented on this "
                "platform";
#endif
}

/**
 * @file UpdateManager.cpp
 * @brief GW2 update detection and enforcement
 *
 * Consolidates all update logic previously scattered across BuildTracker,
 * MainWindow, and DataService. Handles:
 * - Remote build fetching via GW2APIClient
 * - Per-path build ID storage (QSettings)
 * - Exe timestamp verification to detect binary changes
 * - Pre-launch gating (canLaunch)
 * - Crash invalidation (onPatchCrashDetected)
 * - Clean GW2 launch for updates with Job Object monitoring
 *
 * DO NOT ADD:
 * - UI dialogs (belongs in MainWindow)
 * - Profile management (belongs in ProfileManager)
 */

#include "UpdateManager.h"
#include "GW2APIClient.h"
#include "LaunchManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <QWidget>

#include <chrono>
#include <memory>
#include <thread>

// clang-format off
#include <windows.h>
// clang-format on

// === Helper: QSettings factory ===

static std::unique_ptr<QSettings> makeSettings(const QString &settingsPath) {
  if (settingsPath.isEmpty()) {
    return std::make_unique<QSettings>();
  }
  return std::make_unique<QSettings>(settingsPath, QSettings::IniFormat);
}

// === Constructor ===

UpdateManager::UpdateManager(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_settingsPath(settingsPath) {
  runMigrations();
}

// === Dependency Injection ===

void UpdateManager::setApiClient(GW2APIClient *client) {
  m_apiClient = client;
  if (m_apiClient) {
    connect(m_apiClient, &GW2APIClient::buildFetched, this,
            &UpdateManager::onBuildFetched);
  }
}

void UpdateManager::setLaunchManager(LaunchManager *manager) {
  m_launchManager = manager;
}

// === Migrations ===

void UpdateManager::runMigrations() {
  auto settings = makeSettings(m_settingsPath);

  // Migration v1: Clear incorrectly stored build IDs
  if (!settings->value("buildSyncFixApplied", false).toBool()) {
    settings->remove("gw2PathBuildIds");
    settings->remove("gw2LocalBuildId");
    settings->setValue("buildSyncFixApplied", true);
    qInfo() << "Migration v1: Cleared stale build IDs";
  }

  // Migration v2: Clear build IDs stored by faulty update sync
  if (!settings->value("buildSyncV2Applied", false).toBool()) {
    settings->remove("gw2PathBuildIds");
    settings->setValue("buildSyncV2Applied", true);
    qInfo() << "Migration v2: Cleared stale build IDs from faulty update sync";
  }

  // Migration v3: Clear all stored data for UpdateManager consolidation
  // Adds exe timestamp tracking and crash invalidation — old stored builds
  // are unreliable without these safeguards, so force re-verification.
  if (!settings->value("buildSyncV3Applied", false).toBool()) {
    settings->remove("gw2PathBuildIds");
    settings->remove("gw2PathExeTimestamps");
    settings->setValue("buildSyncV3Applied", true);
    qInfo() << "Migration v3: Cleared build data for UpdateManager "
               "consolidation";
  }

  // Corrective migration: detect damage from erroneous v4 that cleared builds
  // when the game was actually up-to-date. If v4 ran (flag exists) but builds
  // are empty, mark for auto-restore when the API build is available.
  if (settings->value("buildSyncV4Applied", false).toBool()) {
    QVariantMap pathBuildIds = settings->value("gw2PathBuildIds").toMap();
    if (pathBuildIds.isEmpty()) {
      m_needsBuildRestore = true;
      qInfo() << "v4fix: Detected cleared build data, will auto-restore on "
                 "next API response";
    }
  }
}

// === Path Normalization (from BuildTracker) ===

QString UpdateManager::normalizePath(const QString &path) {
  QString normalized = path.toLower().replace('\\', '/');
  while (normalized.endsWith('/')) {
    normalized.chop(1);
  }
  return normalized;
}

// === Per-Path Build ID Storage (from BuildTracker) ===

int UpdateManager::getPathBuildId(const QString &gw2Path) const {
  QString normalizedPath = normalizePath(gw2Path);
  auto settings = makeSettings(m_settingsPath);

  QVariantMap pathBuildIds = settings->value("gw2PathBuildIds").toMap();
  return pathBuildIds.value(normalizedPath, 0).toInt();
}

void UpdateManager::setPathBuildId(const QString &gw2Path, int buildId) {
  QString normalizedPath = normalizePath(gw2Path);
  auto settings = makeSettings(m_settingsPath);

  QVariantMap pathBuildIds = settings->value("gw2PathBuildIds").toMap();
  pathBuildIds[normalizedPath] = buildId;
  settings->setValue("gw2PathBuildIds", pathBuildIds);
  qInfo() << "Stored build ID" << buildId << "for path:" << normalizedPath;
}

void UpdateManager::invalidatePathBuild(const QString &gw2Path) {
  setPathBuildId(gw2Path, 0);
  qInfo() << "Invalidated stored build for path:" << normalizePath(gw2Path);
}

void UpdateManager::removeKnownPath(const QString &gw2Path) {
  QString normalizedPath = normalizePath(gw2Path);
  auto settings = makeSettings(m_settingsPath);

  // Remove from build ID map
  QVariantMap pathBuildIds = settings->value("gw2PathBuildIds").toMap();
  pathBuildIds.remove(normalizedPath);
  settings->setValue("gw2PathBuildIds", pathBuildIds);

  // Remove from exe timestamp map
  QVariantMap timestamps = settings->value("gw2PathExeTimestamps").toMap();
  timestamps.remove(normalizedPath);
  settings->setValue("gw2PathExeTimestamps", timestamps);

  qInfo() << "Removed stale known path:" << normalizedPath;
}

QStringList UpdateManager::knownPaths() const {
  auto settings = makeSettings(m_settingsPath);
  QVariantMap pathBuildIds = settings->value("gw2PathBuildIds").toMap();
  return pathBuildIds.keys();
}

// === Exe Timestamp Tracking (NEW) ===

void UpdateManager::storeExeTimestamp(const QString &gw2Path) {
  QString exePath = gw2Path + "/Gw2-64.exe";
  QFileInfo fi(exePath);
  if (!fi.exists()) {
    qWarning() << "Cannot store exe timestamp - file not found:" << exePath;
    return;
  }

  QString normalizedPath = normalizePath(gw2Path);
  qint64 mtime = fi.lastModified().toMSecsSinceEpoch();

  auto settings = makeSettings(m_settingsPath);
  QVariantMap timestamps = settings->value("gw2PathExeTimestamps").toMap();
  timestamps[normalizedPath] = mtime;
  settings->setValue("gw2PathExeTimestamps", timestamps);

  qInfo() << "Stored exe timestamp for" << normalizedPath << ":"
          << fi.lastModified().toString(Qt::ISODate);
}

bool UpdateManager::hasExeChanged(const QString &gw2Path) const {
  QString exePath = gw2Path + "/Gw2-64.exe";
  QFileInfo fi(exePath);
  if (!fi.exists()) {
    return false; // Can't compare if exe doesn't exist
  }

  QString normalizedPath = normalizePath(gw2Path);
  qint64 currentMtime = fi.lastModified().toMSecsSinceEpoch();

  auto settings = makeSettings(m_settingsPath);
  QVariantMap timestamps = settings->value("gw2PathExeTimestamps").toMap();

  if (!timestamps.contains(normalizedPath)) {
    return true; // Never stored → treat as changed (force verification)
  }

  qint64 storedMtime = timestamps.value(normalizedPath).toLongLong();
  bool changed = (currentMtime != storedMtime);

  if (changed) {
    qInfo()
        << "Exe timestamp changed for" << normalizedPath << "stored:"
        << QDateTime::fromMSecsSinceEpoch(storedMtime).toString(Qt::ISODate)
        << "current:"
        << QDateTime::fromMSecsSinceEpoch(currentMtime).toString(Qt::ISODate);
  }

  return changed;
}

// === Update Checking ===

void UpdateManager::checkForUpdates() {
  if (m_buildCheckPending) {
    qInfo() << "GW2 build check already in progress, skipping";
    return;
  }

  if (!m_apiClient) {
    qWarning() << "UpdateManager: No API client set, cannot check for updates";
    return;
  }

  m_buildCheckPending = true;
  qInfo() << "Checking for GW2 updates via API...";

  m_apiClient->fetchBuild();

  // Timeout: reset flag after 10 seconds if no response
  QTimer::singleShot(10000, this, [this]() {
    if (m_buildCheckPending) {
      qWarning() << "GW2 build check timed out (fail-safe)";
      m_buildCheckPending = false;
      emit buildCheckComplete(0);
    }
  });
}

void UpdateManager::onBuildFetched(int remoteBuildId) {
  m_buildCheckPending = false;
  m_remoteBuildId = remoteBuildId;

  qInfo() << "GW2 API returned remote build:" << remoteBuildId;

  // Auto-restore builds cleared by erroneous v4 migration
  if (m_needsBuildRestore && remoteBuildId > 0) {
    auto settings = makeSettings(m_settingsPath);
    QString globalPath = settings->value("gw2Path").toString();
    if (!globalPath.isEmpty()) {
      qInfo() << "v4fix: Auto-restoring build" << remoteBuildId
              << "for path:" << globalPath;
      setPathBuildId(globalPath, remoteBuildId);
      storeExeTimestamp(globalPath);
    }
    m_needsBuildRestore = false;
    qInfo() << "v4fix: Build restore complete";
  }

  emit buildCheckComplete(remoteBuildId);

  // Check all known paths for mismatches
  checkAllKnownPaths();
}

void UpdateManager::checkAllKnownPaths() {
  if (m_remoteBuildId == 0) {
    return; // Can't compare without remote build
  }

  QStringList outdated;
  QStringList stalePaths;

  QStringList paths = knownPaths();
  for (const QString &normalizedPath : paths) {
    // Validate path — check if Gw2-64.exe still exists
    QString exePath = normalizedPath + "/Gw2-64.exe";
    if (!QFileInfo::exists(exePath)) {
      qWarning() << "Stale path detected (Gw2-64.exe missing):"
                 << normalizedPath;
      stalePaths.append(normalizedPath);
      continue; // Skip build comparison for non-existent installs
    }

    // Check if exe has changed since last verification
    if (hasExeChanged(normalizedPath)) {
      qInfo() << "Exe changed for" << normalizedPath
              << "- invalidating stored build";
      invalidatePathBuild(normalizedPath);
    }

    int localBuildId = getPathBuildId(normalizedPath);
    if (m_remoteBuildId != localBuildId) {
      qInfo() << "Build mismatch for" << normalizedPath
              << ": remote=" << m_remoteBuildId << "local=" << localBuildId;
      outdated.append(normalizedPath);
    }
  }

  // Clean up stale paths and notify UI
  if (!stalePaths.isEmpty()) {
    for (const QString &stalePath : stalePaths) {
      removeKnownPath(stalePath);
    }
    qInfo() << "Removed" << stalePaths.size() << "stale path(s)";
    emit stalePathsRemoved(stalePaths);
  }

  if (!outdated.isEmpty()) {
    qInfo() << "Update required for" << outdated.size() << "path(s)";
    emit updateRequired(outdated);
  } else if (!paths.isEmpty()) {
    qInfo() << "All known paths are up to date";
  }
}

// === Pre-Launch Gate ===

bool UpdateManager::canLaunch(const QString &gw2Path) {
  QString checkPath = gw2Path.isEmpty() ? QString() : gw2Path;
  if (checkPath.isEmpty()) {
    qWarning() << "canLaunch: No path provided";
    return true; // Can't check without a path
  }

  // If no cached build data, wait for API
  if (m_remoteBuildId == 0) {
    qInfo() << "Pre-launch check: No cached build data, waiting for API...";

    checkForUpdates();

    // Wait synchronously for up to 5 seconds
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QMetaObject::Connection conn =
        connect(this, &UpdateManager::buildCheckComplete, &loop,
                [&loop](int) { loop.quit(); });

    connect(&timeout, &QTimer::timeout, &loop, [&loop]() {
      qWarning() << "Pre-launch check: API timeout after 5 seconds";
      loop.quit();
    });

    timeout.start(5000);
    loop.exec();
    disconnect(conn);

    if (m_remoteBuildId == 0) {
      qWarning()
          << "Pre-launch check: Could not verify GW2 build - blocking launch";
      emit updateError("Could not verify GW2 version.\n\nPlease check your "
                       "internet connection and try again.");
      return false;
    }
  } else {
    // Trigger background refresh (non-blocking)
    checkForUpdates();
  }

  // Check if exe has changed since last verification
  if (hasExeChanged(checkPath)) {
    qInfo() << "Pre-launch: Exe changed for" << checkPath
            << "- invalidating stored build";
    invalidatePathBuild(checkPath);
  }

  int localBuildId = getPathBuildId(checkPath);

  qInfo() << "Pre-launch check: remote=" << m_remoteBuildId
          << "local=" << localBuildId << "path=" << checkPath;

  if (m_remoteBuildId != localBuildId) {
    qInfo() << "Pre-launch: Build mismatch - blocking launch until updated";
    emit updateRequired(QStringList{checkPath});
    return false;
  }

  return true; // OK to proceed
}

// === Event Handlers ===

void UpdateManager::onProfileLoaded(const QString &gw2Path) {
  if (m_remoteBuildId != 0) {
    qInfo() << "Game loaded successfully - saving build" << m_remoteBuildId
            << "for path:" << gw2Path;
    setPathBuildId(gw2Path, m_remoteBuildId);
    storeExeTimestamp(gw2Path);
  } else {
    qInfo() << "Game loaded successfully from:" << gw2Path
            << "(no remote build cached, skipping build storage)";
  }
}

// NOTE: onPatchCrashDetected() was removed — it was dead code (defined but
// never called). The old binary had it connected to profileCrashDuringLaunch,
// which caused cascading build invalidation on ANY GW2 crash (not just patch
// crashes).

// === Update Execution ===

void UpdateManager::launchUpdateForPath(const QString &gw2Path,
                                        QWidget *parentWidget) {
  Q_UNUSED(parentWidget); // Dialogs are shown by MainWindow via signals

  if (gw2Path.isEmpty()) {
    qWarning() << "launchUpdateForPath: No GW2 path provided";
    emit updateError("GW2 installation path is not configured.");
    return;
  }

  // Resolve the update path
  QString updatePath = gw2Path;
  if (updatePath.endsWith(".exe", Qt::CaseInsensitive)) {
    QFileInfo fi(updatePath);
    updatePath = fi.absolutePath();
  }

  // Find Gw2-64.exe
  QString exePath = updatePath + "/Gw2-64.exe";
  if (!QFileInfo::exists(exePath)) {
    qWarning() << "launchUpdateForPath: Gw2-64.exe not found at" << exePath;
    emit updateError(
        QString("Could not find Gw2-64.exe at:\n%1").arg(updatePath));
    return;
  }

  // Check for running GW2 instances from the same path
  QList<qint64> samePathPids;
  if (m_launchManager) {
    QList<qint64> runningPids = m_launchManager->getRunningGW2Pids();
    for (qint64 pid : runningPids) {
      QString processPath = LaunchManager::getProcessPath(pid);
      if (!processPath.isEmpty() &&
          processPath.startsWith(updatePath, Qt::CaseInsensitive)) {
        samePathPids.append(pid);
        qInfo() << "Found GW2 instance from update path, PID:" << pid
                << "Path:" << processPath;
      }
    }
  }

  // If there are running instances, they need to be terminated first.
  // Emit a signal so the UI can ask for confirmation before proceeding.
  // For now, terminate them (the UI asked before calling this).
  if (!samePathPids.isEmpty()) {
#ifdef Q_OS_WIN
    for (qint64 pid : samePathPids) {
      HANDLE hProcess =
          OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
      if (hProcess) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        qInfo() << "Terminated GW2 process:" << pid;
      }
    }
#endif
    QThread::msleep(1000);
  }

  // Launch GW2 for update via CreateProcess + Job Object
  HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
  if (!hJob) {
    qWarning() << "Failed to create Job Object, falling back to "
                  "QProcess::startDetached";
    QProcess::startDetached(exePath, {}, updatePath);
    return;
  }

  // Create IO Completion Port for Job Object notifications
  HANDLE hCompletionPort =
      CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
  if (!hCompletionPort) {
    qWarning() << "Failed to create IO Completion Port";
    CloseHandle(hJob);
    return;
  }

  // Associate Job with the Completion Port
  JOBOBJECT_ASSOCIATE_COMPLETION_PORT completionPort;
  completionPort.CompletionKey = hJob;
  completionPort.CompletionPort = hCompletionPort;
  SetInformationJobObject(hJob, JobObjectAssociateCompletionPortInformation,
                          &completionPort, sizeof(completionPort));

  // Launch GW2 suspended so we can assign to Job first
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};

  std::wstring exePathW = exePath.toStdWString();
  std::wstring workDirW = updatePath.toStdWString();

  BOOL created =
      CreateProcessW(exePathW.c_str(), nullptr, nullptr, nullptr, FALSE,
                     CREATE_SUSPENDED, nullptr, workDirW.c_str(), &si, &pi);
  if (!created) {
    qWarning() << "CreateProcess failed for" << exePath
               << "error:" << GetLastError();
    CloseHandle(hCompletionPort);
    CloseHandle(hJob);
    return;
  }

  // Assign to Job BEFORE resuming the process
  AssignProcessToJobObject(hJob, pi.hProcess);
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);

  qint64 updatePid = pi.dwProcessId;
  qInfo() << "Launched GW2 for update, PID:" << updatePid
          << "(Job Object assigned)";

  // Monitor in background thread
  // Capture values needed by the thread
  QString capturedUpdatePath = updatePath;
  int capturedRemoteBuild = m_remoteBuildId;

  std::thread([this, capturedUpdatePath, capturedRemoteBuild, hJob,
               hCompletionPort, hProcess = pi.hProcess]() {
    qInfo() << "Update monitor: Job Object mode for path:"
            << capturedUpdatePath;

    auto startTime = std::chrono::steady_clock::now();
    DWORD completionCode = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = nullptr;

    // Wait for Job Object to report all processes exited
    while (GetQueuedCompletionStatus(hCompletionPort, &completionCode,
                                     &completionKey, &overlapped, INFINITE)) {
      if (completionCode == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) {
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        auto elapsedSec =
            std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        qInfo() << "Update monitor: Job Object reports zero processes after"
                << elapsedSec << "seconds";

        if (elapsedSec < 5) {
          // Suspiciously fast — GW2 may have broken away from the Job.
          // Fall back to PID scanning with consecutive checks.
          qInfo() << "Update monitor: Possible breakaway detected - "
                     "falling back to PID scan";

          int consecutiveEmpty = 0;
          while (consecutiveEmpty < 3) {
            Sleep(5000);

            bool foundFromPath = false;
            if (m_launchManager) {
              QList<qint64> pids = m_launchManager->getRunningGW2Pids();
              for (qint64 pid : pids) {
                QString processPath = LaunchManager::getProcessPath(pid);
                if (!processPath.isEmpty() &&
                    processPath.startsWith(capturedUpdatePath,
                                           Qt::CaseInsensitive)) {
                  foundFromPath = true;
                  qInfo() << "Update monitor: Found GW2 from update path, PID:"
                          << pid << "- waiting for exit";

                  HANDLE h =
                      OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
                  if (h) {
                    WaitForSingleObject(h, INFINITE);
                    CloseHandle(h);
                  }
                  consecutiveEmpty = 0;
                  break;
                }
              }
            }

            if (!foundFromPath) {
              consecutiveEmpty++;
              qInfo() << "Update monitor: PID scan empty (" << consecutiveEmpty
                      << "/3)";
            }
          }
        }

        qInfo() << "Update monitor: Update complete";
        break;
      }
    }

    CloseHandle(hJob);
    CloseHandle(hCompletionPort);
    CloseHandle(hProcess);

    // Store build ID + exe timestamp after confirmed update
    // Do time-sensitive work BEFORE queuing to main thread
    if (capturedRemoteBuild > 0) {
      QMetaObject::invokeMethod(
          this,
          [this, capturedUpdatePath, capturedRemoteBuild]() {
            setPathBuildId(capturedUpdatePath, capturedRemoteBuild);
            storeExeTimestamp(capturedUpdatePath);
            qInfo() << "Build synced for" << capturedUpdatePath
                    << "to:" << capturedRemoteBuild;

            emit updateComplete(capturedUpdatePath);

            // Check if other paths also need updating
            checkAllKnownPaths();
          },
          Qt::QueuedConnection);
    } else {
      QMetaObject::invokeMethod(
          this,
          [this, capturedUpdatePath]() {
            emit updateComplete(capturedUpdatePath);
            checkAllKnownPaths();
          },
          Qt::QueuedConnection);
    }
  }).detach();
}

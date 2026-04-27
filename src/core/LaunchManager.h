#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "core/DllInjector.h"
#include "core/LocalDatManager.h"
#include "core/MutexManager.h"
#include "core/ProfileManager.h"
#include "models/LaunchProfile.h"

class GW2WindowWatcher;

/**
 * @brief Manages GW2 process launching with multi-boxing support
 *
 * Ported from LaunchBuddy ClientManager.cs
 * Features: Mutex closing, custom mumble links, Steam support,
 *           DLL injection, window positioning, Local.dat swapping
 */
class LaunchManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(
      QString gw2Path READ gw2Path WRITE setGw2Path NOTIFY gw2PathChanged)
  Q_PROPERTY(bool multiBoxEnabled READ multiBoxEnabled WRITE setMultiBoxEnabled
                 NOTIFY multiBoxEnabledChanged)

public:
  explicit LaunchManager(QObject *parent = nullptr);

  QString gw2Path() const { return m_gw2Path; }
  void setGw2Path(const QString &path);

  bool multiBoxEnabled() const { return m_multiBoxEnabled; }
  void setMultiBoxEnabled(bool enabled) {
    if (m_multiBoxEnabled != enabled) {
      m_multiBoxEnabled = enabled;
      emit multiBoxEnabledChanged();
    }
  }

  /**
   * @brief Launch GW2 with the specified profile
   * @param profile Launch profile containing arguments
   * @return Pointer to the launched process
   */
  QProcess *launchGW2(const LaunchProfile &profile);

  /**
   * @brief Launch GW2 with account profile (includes multi-box handling, DLL
   * injection, window positioning)
   */
  QProcess *launchWithProfile(AccountProfile &profile);

  /**
   * @brief Launch multiple GW2 instances (for multi-boxing)
   */
  QList<QProcess *> launchMultiple(const QList<LaunchProfile> &profiles);

  /**
   * @brief Check if any GW2 instance is currently running
   */
  bool isGW2Running() const;

  /**
   * @brief Get list of running GW2 process IDs
   */
  QList<qint64> getRunningGW2Pids() const;

  /**
   * @brief Build command-line arguments from profile
   */
  QStringList buildArguments(const LaunchProfile &profile) const;

  /**
   * @brief Get the active instance count
   */
  int runningInstanceCount() const { return m_runningProcesses.size(); }

  /**
   * @brief Close GW2 mutex for multi-boxing
   */
  bool closeMutexForMultiBox();

  // Note: backupLocalDat() removed — junction approach does not need
  // pre-backup. Each profile has its own folder.

  /**
   * @brief Deactivate the junction after all launches complete
   * Removes the junction and restores the original AppData folder.
   * Call this after the launch loop finishes (for all profile types).
   */
  void deactivateJunction();

  /**
   * @brief Check if GW2 was installed via Steam
   */
  static bool isSteamInstall(const QString &gw2Path);

  /**
   * @brief Detect Steam GW2 installation path
   */
  static QString detectSteamGW2Path();

  /**
   * @brief Detect Epic Games GW2 installation path
   * Reads Epic manifest files from ProgramData
   */
  static QString detectEpicGW2Path();

  /**
   * @brief Get the effective GW2 path for a profile
   * Resolves custom path, platform-detected path, or global fallback
   * @param profile The profile to resolve the path for
   * @param globalPath The global GW2 path as fallback
   */
  static QString getEffectiveGw2Path(const AccountProfile &profile,
                                     const QString &globalPath);

  /**
   * @brief Check if Steam is currently running
   */
  static bool isSteamRunning();

  /**
   * @brief Check if Epic Games Launcher is currently running
   */
  static bool isEpicRunning();

  /**
   * @brief Get the executable path for a running GW2 process
   * @param pid Process ID
   * @return Full path to the executable, or empty string if not found
   */
  static QString getProcessPath(qint64 pid);

  // Managers
  MutexManager *mutexManager() { return m_mutexManager; }
  DllInjector *dllInjector() { return m_dllInjector; }
  LocalDatManager *localDatManager() { return m_localDatManager; }
  void setLocalDatManager(LocalDatManager *mgr) { m_localDatManager = mgr; }

  /**
   * @brief Get the MumbleLink segment name for a running profile
   * @return Link name (e.g., "GW2MumbleLink2"), or empty string if profile
   *         uses the default "MumbleLink" segment
   */
  QString mumbleLinkNameForProfile(const QString &profileId) const {
    return m_profileMumbleNames.value(profileId);
  }

  /**
   * @brief Set the window title of a GW2 process to include profile name
   * Public so ChildProcessManager can rename windows on reconnect.
   */
  void setWindowTitle(qint64 pid, const QString &profileName);

signals:
  void gw2PathChanged();
  void multiBoxEnabledChanged();
  void gw2Launched(qint64 pid);
  void profileLaunched(const QString &profileId,
                       qint64 pid); // For running state tracking
  void gw2Exited(qint64 pid, int exitCode);
  void profileExited(const QString &profileId); // Profile's GW2 process ended
  void gw2Closed(int exitCode); // For ShellExecuteEx processes
  void
  patchRequired(); // Emitted when GW2 crash log indicates "needs to be patched"

  void launchError(const QString &message);
  void dllInjected(const QString &dllPath, bool success);
  void windowPositioned(qint64 pid, int x, int y);

  /**
   * @brief Emitted from onGW2WindowDetected ONLY — confirms GW2 window exists
   * This means GW2 has read Local.dat and created its mutex.
   * Used as trigger for safe junction switch/deactivation.
   */
  void profileWindowConfirmed(const QString &profileId);

  /**
   * @brief Emitted when a profile crashes during launch (before GPU signal)
   * @param profileId The profile ID that crashed
   * @param profileName The profile display name
   * @param gw2Path The installation path used
   * @param reason Detected reason if available (e.g., "patch required")
   */
  void profileCrashDuringLaunch(const QString &profileId,
                                const QString &profileName,
                                const QString &gw2Path, const QString &reason);

  /**
   * @brief Emitted when a profile successfully loads (GPU signal received)
   * @param gw2Path The installation path that was confirmed working
   */
  void profileLoaded(const QString &gw2Path);

  /**
   * @brief Emitted when a profile reaches character select (credentials fresh)
   * @param profileId The profile that successfully loaded
   */
  void profileCharacterSelectReached(const QString &profileId);

public:
  /**
   * @brief Check ArenaNet.log for "Client needs to be patched" crash
   * @return true if patch is needed
   */
  bool checkForPatchCrash() const;

private slots:
  /**
   * @brief Handle GW2 window detected by GW2WindowWatcher (event-based, no
   * timers)
   */
  void onGW2WindowDetected(qint64 pid, const AccountProfile &profile);

private:
  QString m_gw2Path;
  QString m_gw2Executable;
  QList<QProcess *> m_runningProcesses;
  QMap<QProcess *, qint64> m_processPids; // Store actual PIDs
  QMap<qint64, HANDLE> m_processHandles; // Store process handles for monitoring
  bool m_multiBoxEnabled = true;
  int m_instanceCounter = 0;

  MutexManager *m_mutexManager = nullptr;
  DllInjector *m_dllInjector = nullptr;
  LocalDatManager *m_localDatManager = nullptr;

  void onProcessFinished(int exitCode, QProcess::ExitStatus status);
  QString generateMumbleLinkName();

  // Profile → MumbleLink name mapping (for overlay switching)
  QMap<QString, QString> m_profileMumbleNames;
  // QProcess* → profileId (reverse lookup for cleanup in onProcessFinished)
  QMap<QProcess *, QString> m_processToProfileId;

  // Post-launch actions
  void injectDlls(qint64 pid, const QStringList &dlls);
  void setWindowPosition(qint64 pid, int x, int y, int width, int height);

  // Pending profiles for Steam/Epic (waiting for GPU signal)
  QMap<qint64, AccountProfile> m_pendingProfiles;

  // Crash detection tracking
  QSet<qint64> m_loadedPids; // PIDs that received LOADED signal (GPU active)
  QMap<qint64, QString> m_pidPaths; // PID -> installation path mapping

  // Process monitoring for pre-injection crash detection
  QSet<qint64> m_monitoredPids; // PIDs with active process monitors (prevents
                                // duplicates)
  void startProcessMonitor(HANDLE hProcess, qint64 pid,
                           const AccountProfile &profile,
                           const QString &gw2Path);
};

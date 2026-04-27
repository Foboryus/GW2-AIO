#pragma once

/**
 * @file ChildProcessManager.h
 * @brief Grandfather-side manager for child process lifecycle
 *
 * Creates, monitors, and destroys child processes for each profile.
 * Follows the OverlayInstanceManager pattern: wires to LaunchManager
 * signals internally.
 *
 * Responsibilities:
 * - Creates NTFS hardlinks for per-profile naming (GW2AIO-3d-ProfileName.exe)
 * - Spawns child processes with correct CLI args
 * - Assigns children to a Job Object for guaranteed cleanup
 * - Creates named pipe servers for settings/command IPC
 * - Monitors child process health
 *
 * DO NOT ADD:
 * - Rendering code (belongs in child processes)
 * - UI code (belongs in widgets)
 * - Inline implementations beyond trivial getters (use .cpp)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class LaunchManager;
class ProfileManager;
class QTimer;

/**
 * @brief Tracks one spawned child process
 */
struct ChildProcessInfo {
  QString profileId;
  QString profileName;
  QString featureKey;         // "3d", "minimap", "bigmap", etc.
  QString exePath;            // Hardlink path (GW2AIO-3d-ProfileName.exe)
  HANDLE processHandle = nullptr;
  DWORD processId = 0;
  HANDLE pipeHandle = INVALID_HANDLE_VALUE;
  QString pipeName;
};

class ChildProcessManager : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Construct the manager and wire to LaunchManager signals
   * @param launchManager Provides profileWindowConfirmed / profileExited / profileLaunched
   * @param profileManager Provides profile data (nickname for process naming)
   * @param parent QObject parent
   */
  explicit ChildProcessManager(LaunchManager *launchManager,
                                ProfileManager *profileManager,
                                QObject *parent = nullptr);
  ~ChildProcessManager();

  // --- Child lifecycle ---

  /**
   * @brief Spawn all enabled feature children for a profile
   * @param profileId Profile UUID
   * @param mumbleName MumbleLink segment name
   * @param gw2Pid GW2 process PID
   */
  void spawnChildren(const QString &profileId,
                     const QString &mumbleName,
                     qint64 gw2Pid);

  /**
   * @brief Terminate all children for a profile
   * @param profileId Profile UUID
   */
  void terminateChildren(const QString &profileId);

  /**
   * @brief Terminate a single feature child for a profile
   * @param profileId Profile UUID
   * @param featureKey Feature key ("3d", "minimap", "bigmap", "radial")
   */
  void terminateChild(const QString &profileId, const QString &featureKey);

  /**
   * @brief Terminate ALL children (AIO shutdown)
   */
  void terminateAll();

  /**
   * @brief Send settings to all children of a profile via pipe
   * @param profileId Profile UUID
   * @param settings JSON settings object
   */
  void pushSettings(const QString &profileId, const QJsonObject &settings);

  /**
   * @brief Sync running children with current feature toggle state
   *
   * Compares running children vs MarkerSettingsManager toggles.
   * Kills children for features that were disabled, spawns for enabled.
   * Call when user changes a rendering toggle at runtime.
   *
   * @param profileId Profile UUID
   */
  void syncFeatureToggles(const QString &profileId);

  /**
   * @brief Spawn children for profiles that are already running
   *
   * Called once after construction to handle the AIO-restart-while-GW2-running
   * scenario. Scans ProfileManager::runningProfiles() and spawns children for
   * any running profile that doesn't already have children.
   */
  void spawnForRunningProfiles();

  /**
   * @brief Set the OverlayInstanceManager for runtime toggle wiring
   * Connects to MarkerSettingsManager::settingsChanged signals.
   */
  void setOverlayInstanceManager(class OverlayInstanceManager *mgr);

  // --- Getters ---

  int childCount() const;
  QList<ChildProcessInfo> childrenForProfile(const QString &profileId) const;

signals:
  void childSpawned(const QString &profileId, const QString &featureKey);
  void childTerminated(const QString &profileId, const QString &featureKey);
  void childError(const QString &profileId, const QString &featureKey,
                  const QString &error);

private slots:
  /**
   * @brief Handle LaunchManager::profileWindowConfirmed
   */
  void onProfileWindowConfirmed(const QString &profileId);

  /**
   * @brief Handle LaunchManager::profileLaunched (capture PID)
   */
  void onProfileLaunched(const QString &profileId, qint64 pid);

  /**
   * @brief Handle LaunchManager::profileExited
   */
  void onProfileExited(const QString &profileId);

private:
  Q_DISABLE_COPY_MOVE(ChildProcessManager)

  // --- Hardlink management ---

  QString createHardlink(const QString &featureKey, const QString &profileId);

  // --- Job Object ---

  /**
   * @brief Create/get the Job Object for child process cleanup
   */
  void ensureJobObject();



  /**
   * @brief Start/stop polling child pipes for upstream messages
   */
  void startPipePolling();
  void stopPipePolling();

  /**
   * @brief Poll all child pipe handles for available messages
   * Uses PeekNamedPipe (non-blocking) + ReadFile to consume data.
   */
  void pollChildPipes();

  /**
   * @brief Process a message received from a child
   * @param profileId Profile the sending child belongs to  
   * @param featureKey Feature of the sending child ("overlay", "3d", etc.)
   * @param message The raw message string
   */
  void processChildMessage(const QString &profileId,
                           const QString &featureKey,
                           const QString &message);

  // --- Process spawning ---

  /**
   * @brief Spawn a single child process
   */
  bool spawnChild(const QString &profileId,
                  const QString &mumbleName,
                  qint64 gw2Pid,
                  const QString &featureKey);

  // --- Members ---
  LaunchManager *m_launchManager = nullptr;
  ProfileManager *m_profileManager = nullptr;
  OverlayInstanceManager *m_overlayInstanceMgr = nullptr;

  // Job Object handle (all children assigned here)
  HANDLE m_jobObject = nullptr;

  // profileId → list of spawned children
  QHash<QString, QList<ChildProcessInfo>> m_children;

  // profileId → GW2 PID (cached from profileLaunched signal)
  QHash<QString, qint64> m_profilePids;

  // Profiles whose window was confirmed but PID wasn't available yet.
  // When PID arrives in onProfileLaunched, spawn retroactively.
  QSet<QString> m_pendingSpawn;

  // Directory for hardlinks (lib/ subdirectory next to the exe)
  QString m_childrenDir;

  // Timer for polling child pipes (upstream messages)
  QTimer *m_pipePollingTimer = nullptr;
};

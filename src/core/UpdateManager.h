#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class GW2APIClient;
class LaunchManager;
class QWidget;

/**
 * @brief Manages GW2 update detection and enforcement
 *
 * Consolidates all update-related logic: remote build fetching, per-path build
 * tracking, exe timestamp verification, pre-launch gating, crash invalidation,
 * and clean-launch update management.
 *
 * Replaces BuildTracker (absorbed) and update logic previously in MainWindow.
 *
 * Signals are emitted for UI to show dialogs — no UI code lives here.
 *
 * DO NOT ADD:
 * - UI dialogs (belongs in MainWindow)
 * - Profile management (belongs in ProfileManager)
 */
class UpdateManager : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Construct with settings path for QSettings storage
   * @param settingsPath Path to settings.ini (from StorageBackend)
   */
  explicit UpdateManager(const QString &settingsPath,
                         QObject *parent = nullptr);

  /**
   * @brief Set the API client for remote build fetching
   * Must be called before checkForUpdates()
   */
  void setApiClient(GW2APIClient *client);

  /**
   * @brief Set the launch manager for process queries
   * Needed by launchUpdateForPath() to find running GW2 instances
   */
  void setLaunchManager(LaunchManager *manager);

  // --- Update Checking ---

  /**
   * @brief Fetch remote build from API and check ALL known paths
   * Non-blocking. Emits updateRequired() if any path is outdated.
   * Safe to call multiple times (de-duplicates concurrent checks).
   */
  void checkForUpdates();

  /**
   * @brief Pre-launch gate — can this path launch?
   * Synchronous. Waits up to 5s for API if no cached remote build.
   * Checks exe timestamp for changes since last verification.
   * @param gw2Path Installation directory to check
   * @return true if OK to launch, false if update required
   */
  bool canLaunch(const QString &gw2Path);

  // --- Event Handlers ---

  /**
   * @brief Called when a profile loads successfully (GPU confirmed)
   * Stores the current remote build ID and exe timestamp for this path.
   * This is the ONLY place build IDs are persisted.
   */
  void onProfileLoaded(const QString &gw2Path);

  // NOTE: onPatchCrashDetected() removed — was dead code (never called)

  // --- Update Execution ---

  /**
   * @brief Launch GW2 clean (no arguments) for updating
   * Creates process with Job Object + IO Completion Port for monitoring.
   * Emits updateComplete() when all processes in the Job exit.
   * @param gw2Path Installation directory containing Gw2-64.exe
   * @param parentWidget Parent widget for dialog positioning (can be nullptr)
   */
  void launchUpdateForPath(const QString &gw2Path, QWidget *parentWidget);

  // --- Path Management ---

  /**
   * @brief Remove a known path from both build ID and exe timestamp storage
   * Used to clean up stale paths where GW2 is no longer installed.
   */
  void removeKnownPath(const QString &gw2Path);

  // --- Accessors ---

  int remoteBuildId() const { return m_remoteBuildId; }
  int getPathBuildId(const QString &gw2Path) const;
  QStringList knownPaths() const;
  bool hasExeChanged(const QString &gw2Path) const;
  bool isBuildCheckPending() const { return m_buildCheckPending; }

  /**
   * @brief Normalize a path for consistent key storage
   * Lowercase, forward slashes, no trailing slash
   */
  static QString normalizePath(const QString &path);

signals:
  /**
   * @brief Emitted when one or more paths need updating
   * @param outdatedPaths List of paths with build mismatches
   */
  void updateRequired(const QStringList &outdatedPaths);

  /**
   * @brief Emitted when an update completes for a path
   */
  void updateComplete(const QString &path);

  /**
   * @brief Emitted when API build check completes (success or timeout)
   */
  void buildCheckComplete(int remoteBuildId);

  /**
   * @brief Emitted on update-related errors (API timeout, exe not found, etc.)
   */
  void updateError(const QString &message);

  /**
   * @brief Emitted when stale paths (no Gw2-64.exe found) are removed
   * @param removedPaths Paths that were cleaned up
   */
  void stalePathsRemoved(const QStringList &removedPaths);

private:
  // --- Internal Logic ---
  void onBuildFetched(int remoteBuildId);
  void checkAllKnownPaths();
  void invalidatePathBuild(const QString &gw2Path);
  void setPathBuildId(const QString &gw2Path, int buildId);
  void storeExeTimestamp(const QString &gw2Path);
  void runMigrations();

  // --- State ---
  int m_remoteBuildId = 0;
  bool m_buildCheckPending = false;
  bool m_needsBuildRestore = false; // Set by corrective migration v4fix
  QString m_settingsPath;
  GW2APIClient *m_apiClient = nullptr;
  LaunchManager *m_launchManager = nullptr;
};

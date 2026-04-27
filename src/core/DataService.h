#pragma once

/**
 * @brief Centralized Data Service — single gateway for all data operations
 *
 * Owns ProfileManager, SettingsManager, UpdateManager, LocalDatManager,
 * and GFXManager. All data reads/writes should go through this class.
 *
 * Prepared for future:
 * - Cloud sync (change signals for every mutation)
 * - Portable mode (StorageBackend abstraction)
 * - Cross-OS (path normalization)
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Inline implementations (use DataService.cpp)
 */

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariant>

class ProfileManager;
class SettingsManager;
class UpdateManager;
class StorageBackend;
class LocalDatManager;
class GFXManager;
class ActivationStore;
class MarkerSettingsManager;
class APIKeyManager;
class APICache;
class GW2APIClient;
struct AccountProfile;

class DataService : public QObject {
  Q_OBJECT

public:
  explicit DataService(QObject *parent = nullptr);
  ~DataService();

  // === Direct access (for callers that need the full API during transition)
  // === These will be removed as callers are fully migrated
  ProfileManager *profileManager() const { return m_profileManager; }
  SettingsManager *settingsManager() const { return m_settingsManager; }
  UpdateManager *updateManager() const { return m_updateManager; }
  StorageBackend *storageBackend() const { return m_storageBackend; }

  // =========================================================================
  // PROFILE OPERATIONS
  // =========================================================================

  /// @brief Get all profiles (read-only copy)
  QList<AccountProfile> profiles() const;

  /// @brief Get a mutable pointer to a profile by ID
  AccountProfile *profile(const QString &id);

  /// @brief Get a profile by nickname
  AccountProfile *profileByNickname(const QString &nickname);

  /// @brief Add a new profile with the given nickname, returns its UUID
  QString addProfile(const QString &nickname);

  /// @brief Add a fully populated profile (e.g., from import or wizard)
  void addCompleteProfile(const AccountProfile &profile);

  /// @brief Update an existing profile (atomic write to disk)
  bool updateProfile(const AccountProfile &profile);

  /// @brief Remove a profile by ID
  bool removeProfile(const QString &id);

  /// @brief Reorder profiles in the display list
  bool moveProfile(int fromIndex, int toIndex);

  // --- Targeted profile mutations (prevents direct field access) ---

  /// @brief Set the last login time for a profile
  void setLastLoginTime(const QString &profileId, const QDateTime &time);

  /// @brief Set the Local.dat path for a profile
  void setLocalDatPath(const QString &profileId, const QString &path);

  /// @brief Set the GFX settings path for a profile
  void setGfxSettingsPath(const QString &profileId, const QString &path);

  // --- Import/Export ---

  bool exportProfile(const QString &id, const QString &filePath);
  bool importProfile(const QString &filePath);
  bool exportAllProfiles(const QString &folderPath);

  /// @brief Import with post-import integrity checks + build sync.
  /// Returns true if import succeeded. Writes the resolved GW2 path to
  /// outGw2Path (may be empty if no path configured).
  bool importProfileWithChecks(const QString &filePath, QString &outGw2Path);

  // --- Default profile ---

  QString defaultProfileId() const;
  void setDefaultProfileId(const QString &id);
  AccountProfile *defaultProfile();

  // --- Running state ---

  bool isProfileRunning(const QString &id);
  qint64 getProfilePid(const QString &id) const;
  void setProfileRunning(const QString &id, qint64 pid);
  void clearProfileRunning(const QString &id);
  void validateRunningProfiles();
  QMap<QString, qint64> runningProfiles() const;
  bool bringProfileWindowToFocus(const QString &id);
  bool minimizeProfileWindow(const QString &id);

  // --- Profile persistence ---

  bool loadProfiles();
  bool saveProfiles();

  // =========================================================================
  // GLOBAL SETTINGS
  // =========================================================================

  /// @brief Get a setting value
  QVariant setting(const QString &key,
                   const QVariant &defaultValue = QVariant()) const;

  /// @brief Set a setting value
  void setSetting(const QString &key, const QVariant &value);

  /// @brief Check if a setting key exists
  bool hasSetting(const QString &key) const;

  /// @brief Remove a setting key
  void removeSetting(const QString &key);

  // --- Typed convenience accessors ---

  QString gw2Path() const;
  void setGw2Path(const QString &path);

  bool showTrayIcon() const;
  void setShowTrayIcon(bool show);

  bool startMinimized() const;
  void setStartMinimized(bool minimize);

  bool checkUpdates() const;
  void setCheckUpdates(bool check);

  bool cefCleanup() const;
  void setCefCleanup(bool clean);

  int selectedTheme() const;
  void setSelectedTheme(int theme);

  /// @brief Sync settings to disk
  void syncSettings();

  /// @brief Get settings file path (for display/debug)
  QString settingsFilePath() const;

  /// @brief Get saved Local.dat directory
  QString savedDatsDir() const;

  /// @brief Get saved GFX settings directory
  QString savedGfxDir() const;

  // =========================================================================
  // LOCAL DAT & GFX MANAGERS
  // =========================================================================

  /// @brief Get the LocalDatManager instance
  LocalDatManager *localDatManager();

  /// @brief Get the GFXManager instance
  GFXManager *gfxManager();

  /// @brief Get the ActivationStore instance
  ActivationStore *activationStore();

  /// @brief Get the MarkerSettingsManager instance
  MarkerSettingsManager *markerSettings();

  /// @brief Get the APIKeyManager instance
  APIKeyManager *apiKeyManager();

  /// @brief Get the APICache instance
  APICache *apiCache();

  /// @brief Get the GW2APIClient instance
  GW2APIClient *apiClient();

  // =========================================================================
  // UPDATE MANAGER
  // =========================================================================

signals:
  // --- Profile signals ---
  void profilesChanged();
  void profileAdded(const QString &id);
  void profileUpdated(const QString &id);
  void profileRemoved(const QString &id);
  void profileOrderChanged();
  void profileRunningStateChanged(const QString &id, bool running);
  void profilesRestoredFromBackup();

  // --- Settings signals ---
  void settingChanged(const QString &key, const QVariant &value);

private:
  StorageBackend *m_storageBackend;
  ProfileManager *m_profileManager;
  SettingsManager *m_settingsManager;
  UpdateManager *m_updateManager;
  LocalDatManager *m_localDatManager;
  GFXManager *m_gfxManager;
  ActivationStore *m_activationStore;
  MarkerSettingsManager *m_markerSettings;
  GW2APIClient *m_apiClient;
  APIKeyManager *m_apiKeyManager;
  APICache *m_apiCache;
};

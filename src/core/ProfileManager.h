#pragma once

/**
 * @brief Profile Manager - Ported from LaunchBuddy AccountManager.cs
 *
 * Manages multiple GW2 accounts/profiles with per-account settings.
 *
 * Original: https://github.com/TheCheatsrichter/Gw2_Launchbuddy
 */

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QTimer>
#include <QUuid>

// clang-format off
#ifdef Q_OS_WIN
#include <Windows.h>
#include <Psapi.h>
#endif
// clang-format on

#include "models/LaunchProfile.h"

/**
 * @brief Network mode for per-profile network settings
 */
enum class NetworkMode {
  UseDefault =
      0, // No custom args (ArenaNet defaults) - DEFAULT for new profiles
  UseGlobal = 1, // Follow Global Network tab settings
  Custom = 2     // Profile-specific server
};

/**
 * @brief Account provider for login authentication
 */
enum class AccountProvider {
  Standalone = 0, // ArenaNet standalone account (no -provider arg) - DEFAULT
  Steam = 1,      // Steam account (-provider Steam)
  Epic = 2        // Epic Games account (-provider Epic)
};

/**
 * @brief Extended profile with account-specific settings
 */
struct AccountProfile {
  QString id;                                // Unique identifier
  QString nickname;                          // Display name
  QString icon = ":/icons/profile-game.svg"; // Profile icon (SVG resource path)
  QString color = "#C09C5B";                 // Profile color
  QString localDatPath;      // Custom Local.dat path for fast login
  QString gfxSettingsPath;   // Custom GFX settings
  bool useCustomGfx = false; // Apply custom GFX settings on launch

  // Launch settings
  QStringList arguments; // Launch arguments
  AccountProvider accountProvider =
      AccountProvider::Standalone; // Standalone/Steam/Epic
  bool autoLogin = false;          // Use Local.dat for auto-login
  bool useCustomMumble = false;    // Custom mumble link name
  QString mumbleLinkName;          // e.g., "GW2MumbleLink1"

  // Window settings
  bool useCustomWindow = false;
  int windowX = 0;
  int windowY = 0;
  int windowWidth = 1920;
  int windowHeight = 1080;

  // Process settings
  int processPriority = 0;  // 0=Normal, 1=Above, 2=High
  quint64 affinityMask = 0; // CPU affinity (0 = all cores)

  // Addon settings
  QStringList enabledAddons; // List of enabled addon IDs
  QStringList injectedDlls;  // DLLs to inject

  // Tracking
  int relaunchCount = 0; // Auto-relaunch on crash
  int relaunchesLeft = 0;

  // Network settings
  NetworkMode networkMode =
      NetworkMode::UseDefault; // Default: no args (like unmodified launcher)
  QString customNetworkServer; // Only used when networkMode == Custom (e.g.,
                               // "64.25.38.54:6112")

  // Custom GW2 installation path (per-profile)
  bool useCustomGw2Path = false;
  QString customGw2Path; // Path to Gw2-64.exe if not using global

  // Global hotkeys (per-profile)
  QString hotkeyFocus;    // e.g., "Ctrl+F1" — bring window to front
  QString hotkeyMinimize; // e.g., "Ctrl+Shift+F1" — minimize window

  // Session tracking
  QDateTime lastLoginTime;         // Last time this profile was launched
  QDateTime lastCloseTime;         // Last time GW2 was closed for this profile
  qint64 totalPlaytimeSeconds = 0; // Total time GW2 was running
  qint64 activePlaytimeSeconds =
      0; // Time GW2 was in foreground (if tracking enabled)

  // GW2 API integration
  QString gw2ApiKey; // API key for achievement progress tracking

  // Per-profile build verification
  // When GW2 updates, each profile's Local.dat needs individual updating.
  // This tracks which build this profile's dat was last verified against.
  int lastVerifiedBuild = 0;

  QJsonObject toJson() const {
    QJsonObject obj;
    obj["schemaVersion"] = 2;
    obj["id"] = id;
    obj["nickname"] = nickname;
    obj["icon"] = icon;
    obj["color"] = color;
    obj["localDatPath"] = localDatPath;
    obj["gfxSettingsPath"] = gfxSettingsPath;
    obj["useCustomGfx"] = useCustomGfx;
    obj["arguments"] = QJsonArray::fromStringList(arguments);
    obj["accountProvider"] = static_cast<int>(accountProvider);
    obj["autoLogin"] = autoLogin;
    obj["useCustomMumble"] = useCustomMumble;
    obj["mumbleLinkName"] = mumbleLinkName;
    obj["useCustomWindow"] = useCustomWindow;
    obj["windowX"] = windowX;
    obj["windowY"] = windowY;
    obj["windowWidth"] = windowWidth;
    obj["windowHeight"] = windowHeight;
    obj["processPriority"] = processPriority;
    obj["affinityMask"] = QString::number(affinityMask);
    obj["enabledAddons"] = QJsonArray::fromStringList(enabledAddons);
    obj["injectedDlls"] = QJsonArray::fromStringList(injectedDlls);
    obj["relaunchCount"] = relaunchCount;

    // Network settings
    obj["networkMode"] = static_cast<int>(networkMode);
    obj["customNetworkServer"] = customNetworkServer;

    // Custom GW2 path
    obj["useCustomGw2Path"] = useCustomGw2Path;
    obj["customGw2Path"] = customGw2Path;

    // Session tracking
    obj["lastLoginTime"] = lastLoginTime.toString(Qt::ISODate);
    obj["lastCloseTime"] = lastCloseTime.toString(Qt::ISODate);
    obj["totalPlaytimeSeconds"] = totalPlaytimeSeconds;
    obj["activePlaytimeSeconds"] = activePlaytimeSeconds;

    // GW2 API key
    if (!gw2ApiKey.isEmpty()) {
      obj["gw2ApiKey"] = gw2ApiKey;
    }

    // Per-profile build verification
    obj["lastVerifiedBuild"] = lastVerifiedBuild;

    return obj;
  }

  static AccountProfile fromJson(const QJsonObject &obj) {
    AccountProfile p;
    int version = obj["schemaVersion"].toInt(0);
    // v1: initial, v2: hotkeys moved to separate file, v3+: future
    Q_UNUSED(version);
    p.id = obj["id"].toString();
    p.nickname = obj["nickname"].toString();
    p.icon = obj["icon"].toString(":/icons/profile-game.svg");
    p.color = obj["color"].toString("#C09C5B");
    // email/encryptedPassword removed — see
    // docs/archived-features/keyboard-autologin.md
    p.localDatPath = obj["localDatPath"].toString();
    p.gfxSettingsPath = obj["gfxSettingsPath"].toString();
    p.useCustomGfx = obj["useCustomGfx"].toBool();

    QJsonArray argsArray = obj["arguments"].toArray();
    for (const auto &arg : argsArray) {
      p.arguments.append(arg.toString());
    }

    // Account provider (with backward compatibility for old useSteam field)
    if (obj.contains("accountProvider")) {
      p.accountProvider =
          static_cast<AccountProvider>(obj["accountProvider"].toInt(0));
    } else if (obj["useSteam"].toBool()) {
      // Migrate old useSteam=true to Steam provider
      p.accountProvider = AccountProvider::Steam;
    } else {
      p.accountProvider = AccountProvider::Standalone;
    }
    p.autoLogin = obj["autoLogin"].toBool();
    // Phase 7: auto-generate persistent mumble link name if missing
    p.mumbleLinkName = obj["mumbleLinkName"].toString();
    if (p.mumbleLinkName.isEmpty()) {
      // Use first 8 chars of profile UUID for a human-readable unique name
      p.mumbleLinkName = QStringLiteral("GW2Mumble_%1").arg(p.id.left(8));
      qInfo() << "[DEV] ProfileManager: auto-generated mumbleLinkName:"
              << p.mumbleLinkName << "for profile:" << p.id;
    }
    p.useCustomMumble = true; // Always true now — every profile has a unique name
    p.useCustomWindow = obj["useCustomWindow"].toBool();
    p.windowX = obj["windowX"].toInt();
    p.windowY = obj["windowY"].toInt();
    p.windowWidth = obj["windowWidth"].toInt(1920);
    p.windowHeight = obj["windowHeight"].toInt(1080);
    p.processPriority = obj["processPriority"].toInt();
    p.affinityMask = obj["affinityMask"].toString().toULongLong();

    QJsonArray addonsArray = obj["enabledAddons"].toArray();
    for (const auto &addon : addonsArray) {
      p.enabledAddons.append(addon.toString());
    }

    QJsonArray dllsArray = obj["injectedDlls"].toArray();
    for (const auto &dll : dllsArray) {
      p.injectedDlls.append(dll.toString());
    }

    p.relaunchCount = obj["relaunchCount"].toInt();

    // Network settings (default to UseDefault for backward compatibility)
    p.networkMode = static_cast<NetworkMode>(obj["networkMode"].toInt(0));
    p.customNetworkServer = obj["customNetworkServer"].toString();

    // Custom GW2 path
    p.useCustomGw2Path = obj["useCustomGw2Path"].toBool();
    p.customGw2Path = obj["customGw2Path"].toString();

    // Global hotkeys — loaded from separate file by ProfileManager
    // Backward compat: if present in profile JSON (v2 migration), read them
    if (obj.contains("hotkeyFocus"))
      p.hotkeyFocus = obj["hotkeyFocus"].toString();
    if (obj.contains("hotkeyMinimize"))
      p.hotkeyMinimize = obj["hotkeyMinimize"].toString();

    // Session tracking
    p.lastLoginTime =
        QDateTime::fromString(obj["lastLoginTime"].toString(), Qt::ISODate);
    p.lastCloseTime =
        QDateTime::fromString(obj["lastCloseTime"].toString(), Qt::ISODate);
    p.totalPlaytimeSeconds =
        obj["totalPlaytimeSeconds"].toVariant().toLongLong();
    p.activePlaytimeSeconds =
        obj["activePlaytimeSeconds"].toVariant().toLongLong();

    // GW2 API key
    p.gw2ApiKey = obj["gw2ApiKey"].toString();

    // Per-profile build verification
    p.lastVerifiedBuild = obj["lastVerifiedBuild"].toInt(0);

    return p;
  }

  // Convert to basic LaunchProfile for launching
  LaunchProfile toLaunchProfile() const {
    LaunchProfile lp;
    lp.name = nickname;
    lp.arguments = arguments;

    // Note: -shareArchive is added by LaunchManager::launchGW2() only when
    // multi-boxing is active. Adding it unconditionally breaks GW2's streaming
    // downloader on single-instance launches.

    // Add -autologin when fast login is enabled
    if (autoLogin && !localDatPath.isEmpty()) {
      if (!lp.arguments.contains("-autologin")) {
        lp.arguments.append("-autologin");
      }
    }

    // Add custom mumble link if needed
    if (useCustomMumble && !mumbleLinkName.isEmpty()) {
      lp.arguments.append("-mumble");
      lp.arguments.append(mumbleLinkName);
    }

    // Add -windowed flag only when custom window size is enabled
    // Actual positioning happens after Play button is clicked (Mumble Link
    // detection)
    if (useCustomWindow) {
      if (!lp.arguments.contains("-windowed")) {
        lp.arguments.append("-windowed");
      }
      // Note: -left/-top/-width/-height not used because they would position
      // the splash screen too. Positioning is done post-launch via
      // setWindowPosition after Mumble Link detects the game is actually
      // running.
    }

    // Note: -email and -password args were removed by ArenaNet
    // Fast login now uses Local.dat file swapping

    lp.processPriority = processPriority;

    return lp;
  }
};

class ProfileManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(
      QList<AccountProfile> profiles READ profiles NOTIFY profilesChanged)

public:
  explicit ProfileManager(QObject *parent = nullptr);
  explicit ProfileManager(const QString &profilesDir,
                          QObject *parent = nullptr);
  explicit ProfileManager(const QString &profilesDir,
                          const QString &savedDatsDir,
                          const QString &savedGfxDir,
                          const QString &savedHotkeysDir,
                          const QString &markerStateDir = {},
                          QObject *parent = nullptr);

  // Profile management
  QList<AccountProfile> profiles() const { return m_profiles; }
  AccountProfile *profile(const QString &id);
  AccountProfile *profileByNickname(const QString &nickname);

  QString addProfile(const QString &nickname);
  void addCompleteProfile(
      const AccountProfile &profile); // Add a fully populated profile
  bool removeProfile(const QString &id);
  bool updateProfile(const AccountProfile &profile);
  bool moveProfile(int fromIndex, int toIndex);

  // Persistence
  bool load();
  bool save(); // Deprecated - saves manifest only, use saveProfile() for
               // individual profiles

  // Import/Export
  bool exportProfile(const QString &id, const QString &filePath);
  bool importProfile(const QString &filePath);
  bool exportAllProfiles(const QString &folderPath);

  // Default profile
  QString defaultProfileId() const { return m_defaultProfileId; }
  void setDefaultProfileId(const QString &id);
  AccountProfile *defaultProfile();

  // Legacy password/email fields removed — see:
  // docs/archived-features/legacy-password-login.md (ArenaNet removed
  // -email/-password CLI) docs/archived-features/keyboard-autologin.md (DPAPI
  // not portable)

  // Running profile tracking
  bool
  isProfileRunning(const QString &id); // Non-const: auto-cleans dead entries
  qint64 getProfilePid(const QString &id) const;
  void setProfileRunning(const QString &id, qint64 pid,
                         const QString &mumbleLinkName = {});
  void clearProfileRunning(const QString &id);
  void validateRunningProfiles(); // Check all PIDs, remove stale entries
  QMap<QString, qint64> runningProfiles() const { return m_runningProfiles; }
  QString mumbleLinkNameForRunningProfile(const QString &id) const {
    return m_runningMumbleNames.value(id);
  }
  bool bringProfileWindowToFocus(
      const QString &id); // Try to focus running profile's window
  bool minimizeProfileWindow(
      const QString &id); // Try to minimize running profile's window

signals:
  void profilesChanged();
  void profileAdded(const QString &id);
  void profileRemoved(const QString &id);
  void profileUpdated(const QString &id);
  void profileRunningStateChanged(const QString &id, bool running);
  void profilesRestoredFromBackup(); // Notify UI when auto-recovery occurs

private:
  QList<AccountProfile> m_profiles;
  QString m_defaultProfileId;
  QStringList m_profileOrder; // Profile IDs in display order

  // Per-profile file storage paths
  QString m_profilesDir;  // profiles/ folder
  QString m_manifestPath; // profiles/manifest.json
  QString m_runningProfilesPath;
  QMap<QString, qint64> m_runningProfiles; // profileId -> PID
  QMap<QString, QString> m_runningMumbleNames; // profileId -> mumble link name

  // Base directories for relative path conversion
  QString m_savedDatsDir;
  QString m_savedGfxDir;
  QString m_savedHotkeysDir;
  QString m_markerStateDir;

  // Helper methods
  QString generateId() const;
  QString profileFilePath(const QString &id) const;

  // Atomic file operations
  bool saveProfileToFile(const AccountProfile &profile);
  bool loadProfileFromFile(const QString &id, AccountProfile &out);
  bool saveManifest();
  bool loadManifest();
  bool loadHotkeys(AccountProfile &profile);
  bool saveHotkeys(const AccountProfile &profile);

  // Path conversion helpers for portable profiles
  QString toRelativePath(const QString &absolutePath,
                         const QString &baseDir) const;
  QString toAbsolutePath(const QString &relativePath,
                         const QString &baseDir) const;

  // Running profiles (unchanged)
  void loadRunningProfiles();
  void saveRunningProfiles();
  bool isProcessAlive(qint64 pid) const;
};

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>

// Centralized version string - update here only
#define APP_VERSION "0.1.0-alpha"

/**
 * @brief Application configuration and path management
 *
 * Supports both installed and portable modes.
 * Portable mode is activated by placing a 'portable.txt' file next to the exe.
 */
class AppConfig : public QObject {
  Q_OBJECT

public:
  static AppConfig &instance();

  /**
   * @brief Initialize configuration
   */
  void initialize();

  /**
   * @brief Check if running in portable mode
   */
  bool isPortable() const { return m_portable; }

  /**
   * @brief Get data directory (settings, logs, etc.)
   */
  QString dataDir() const { return m_dataDir; }

  /**
   * @brief Get logs directory
   */
  QString logsDir() const { return QDir(m_dataDir).filePath("logs"); }

  /**
   * @brief Get crashes directory
   */
  QString crashesDir() const { return QDir(m_dataDir).filePath("crashes"); }

  /**
   * @brief Get marker packs directory
   */
  QString markerPacksDir() const {
    return QDir(m_dataDir).filePath("MarkerPacks");
  }

  /**
   * @brief Get marker packs cache directory (extracted .taco contents)
   */
  QString markerPacksCacheDir() const {
    return QDir(m_dataDir).filePath("MarkerPacksCache");
  }

  /**
   * @brief Get Blish modules directory
   */
  QString blishModulesDir() const {
    return QDir(m_dataDir).filePath("BlishModules");
  }

  /**
   * @brief Get radial configs directory
   */
  QString radialConfigDir() const {
    return QDir(m_dataDir).filePath("RadialMenus");
  }

  /**
   * @brief Get profile data root directory
   *
   * SYNC: Must match LocalStorageBackend::profileDataDir().
   * Child processes use AppConfig (not StorageBackend/DataService)
   * because they are separate executables without DataService access.
   * If LocalStorageBackend changes this path, update here too.
   */
  QString profileDataDir() const {
    return QDir(m_dataDir).filePath("ProfileData");
  }

  /**
   * @brief Get marker state directory (per-profile marker settings)
   *
   * SYNC: Must match LocalStorageBackend::markerStateDir().
   * See profileDataDir() comment for rationale.
   */
  QString markerStateDir() const {
    return QDir(profileDataDir()).filePath("marker_state");
  }

  /**
   * @brief Get settings file path
   */
  QString settingsPath() const {
    return QDir(m_dataDir).filePath("settings.ini");
  }

  /**
   * @brief Ensure all directories exist
   */
  void ensureDirectories();

  /**
   * @brief Get application executable path
   */
  QString executablePath() const { return m_exePath; }

  /**
   * @brief Get application directory
   */
  QString applicationDir() const { return m_appDir; }

signals:
  void configurationChanged();

private:
  AppConfig() = default;

  bool checkPortableMode();

  bool m_portable = false;
  QString m_dataDir;
  QString m_exePath;
  QString m_appDir;
};

// Implementation
inline AppConfig &AppConfig::instance() {
  static AppConfig instance;
  return instance;
}

inline void AppConfig::initialize() {
  m_exePath = QCoreApplication::applicationFilePath();
  m_appDir = QCoreApplication::applicationDirPath();

  // If launched via the stub, GW2AIO_ROOT points to the true root directory
  // (where the stub exe and portable.txt live). Without this, m_appDir would
  // be lib/ (where the real Qt exe lives), breaking portable detection.
  QString rootOverride = qEnvironmentVariable("GW2AIO_ROOT");
  if (!rootOverride.isEmpty()) {
    // Normalize: remove trailing slash/backslash for consistency
    while (rootOverride.endsWith('/') || rootOverride.endsWith('\\')) {
      rootOverride.chop(1);
    }
    m_appDir = rootOverride;
    qInfo() << "Root directory (from launcher stub):" << m_appDir;
  }

  // Check for portable mode
  m_portable = checkPortableMode();

  if (m_portable) {
    m_dataDir = QDir(m_appDir).filePath("data");
    qInfo() << "Running in PORTABLE mode";
  } else {
    m_dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    qInfo() << "Running in INSTALLED mode";
  }

  qInfo() << "Data directory:" << m_dataDir;

  ensureDirectories();
}

inline bool AppConfig::checkPortableMode() {
  // Check for portable.txt next to executable
  QString portableFile = QDir(m_appDir).filePath("portable.txt");
  if (QFile::exists(portableFile)) {
    return true;
  }

  // Also check for portable.ini
  QString portableIni = QDir(m_appDir).filePath("portable.ini");
  if (QFile::exists(portableIni)) {
    return true;
  }

  // Check if data folder exists next to exe (previous portable install)
  QString dataFolder = QDir(m_appDir).filePath("data");
  if (QDir(dataFolder).exists()) {
    return true;
  }

  return false;
}

inline void AppConfig::ensureDirectories() {
  QDir().mkpath(m_dataDir);
  QDir().mkpath(logsDir());
  QDir().mkpath(crashesDir());
  QDir().mkpath(markerPacksDir());
  QDir().mkpath(markerPacksCacheDir());
  QDir().mkpath(blishModulesDir());
  QDir().mkpath(radialConfigDir());
  QDir().mkpath(profileDataDir());
  QDir().mkpath(markerStateDir());
}

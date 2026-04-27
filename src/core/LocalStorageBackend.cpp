/**
 * @file LocalStorageBackend.cpp
 * @brief Local filesystem storage backend implementation
 *
 * Delegates to AppConfig for path resolution.
 * All paths are derived from the single dataDir() root.
 *
 * DO NOT ADD:
 * - Data read/write logic (belongs in managers)
 * - UI code (belongs in widgets)
 */

#include "LocalStorageBackend.h"
#include "AppConfig.h"

#include <QDir>

LocalStorageBackend::LocalStorageBackend() {
  // Pull current state from AppConfig (already initialized by main.cpp)
  m_dataDir = AppConfig::instance().dataDir();
  m_portable = AppConfig::instance().isPortable();
}

bool LocalStorageBackend::isPortable() const { return m_portable; }

QString LocalStorageBackend::dataDir() const { return m_dataDir; }

QString LocalStorageBackend::profilesDir() const {
  return QDir(m_dataDir).filePath("profiles");
}

QString LocalStorageBackend::settingsFilePath() const {
  // Must match QSettings(IniFormat, UserScope, "GW2AIO", "GW2AIO") path.
  // QSettings::setPath() in main.cpp sets dataDir as the root,
  // then QSettings adds "GW2AIO/GW2AIO.ini" under it.
  return QDir(m_dataDir).filePath("GW2AIO/GW2AIO.ini");
}

QString LocalStorageBackend::logsDir() const {
  return QDir(m_dataDir).filePath("logs");
}

QString LocalStorageBackend::crashesDir() const {
  return QDir(m_dataDir).filePath("crashes");
}

QString LocalStorageBackend::savedDatsDir() const {
  return QDir(m_dataDir).filePath("SavedDats");
}

QString LocalStorageBackend::savedGfxDir() const {
  return QDir(m_dataDir).filePath("SavedGfx");
}

QString LocalStorageBackend::savedHotkeysDir() const {
  return QDir(m_dataDir).filePath("SavedHotkeys");
}

QString LocalStorageBackend::markerPacksDir() const {
  return QDir(m_dataDir).filePath("MarkerPacks");
}

QString LocalStorageBackend::markerPacksCacheDir() const {
  return QDir(m_dataDir).filePath("MarkerPacksCache");
}

QString LocalStorageBackend::blishModulesDir() const {
  return QDir(m_dataDir).filePath("BlishModules");
}

QString LocalStorageBackend::radialConfigDir() const {
  return QDir(m_dataDir).filePath("RadialMenus");
}

QString LocalStorageBackend::profileDataDir() const {
  return QDir(m_dataDir).filePath("ProfileData");
}

QString LocalStorageBackend::markerStateDir() const {
  return QDir(profileDataDir()).filePath("marker_state");
}

QString LocalStorageBackend::apiCacheDir() const {
  return QDir(m_dataDir).filePath("api_cache");
}

void LocalStorageBackend::ensureDirectories() {
  QDir().mkpath(m_dataDir);
  QDir().mkpath(profilesDir());
  QDir().mkpath(logsDir());
  QDir().mkpath(crashesDir());
  QDir().mkpath(savedDatsDir());
  QDir().mkpath(savedGfxDir());
  QDir().mkpath(savedHotkeysDir());
  QDir().mkpath(markerPacksDir());
  QDir().mkpath(markerPacksCacheDir());
  QDir().mkpath(blishModulesDir());
  QDir().mkpath(radialConfigDir());
  QDir().mkpath(profileDataDir());
  QDir().mkpath(markerStateDir());
  QDir().mkpath(apiCacheDir());
}

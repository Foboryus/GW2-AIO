#pragma once

/**
 * @brief Local filesystem storage backend
 *
 * Wraps AppConfig to provide StorageBackend interface.
 * Supports both installed and portable modes.
 *
 * DO NOT ADD:
 * - Data read/write logic (belongs in managers)
 * - Cloud/network logic (future CloudStorageBackend)
 */

#include "StorageBackend.h"

class LocalStorageBackend : public StorageBackend {
public:
  LocalStorageBackend();

  // --- StorageBackend interface ---
  bool isPortable() const override;
  QString dataDir() const override;
  QString profilesDir() const override;
  QString settingsFilePath() const override;
  QString logsDir() const override;
  QString crashesDir() const override;
  QString savedDatsDir() const override;
  QString savedGfxDir() const override;
  QString savedHotkeysDir() const override;
  QString markerPacksDir() const override;
  QString markerPacksCacheDir() const override;
  QString blishModulesDir() const override;
  QString radialConfigDir() const override;
  QString profileDataDir() const override;
  QString markerStateDir() const override;
  void ensureDirectories() override;

private:
  QString m_dataDir;
  bool m_portable;
};

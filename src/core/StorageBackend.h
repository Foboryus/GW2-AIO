#pragma once

/**
 * @brief Abstract interface for storage path resolution
 *
 * Provides all directory/file paths needed by the data layer.
 * Implementations determine where data lives (local disk, portable, cloud).
 *
 * DO NOT ADD:
 * - Data read/write logic (belongs in managers)
 * - UI code (belongs in widgets)
 */

#include <QString>

class StorageBackend {
public:
  virtual ~StorageBackend() = default;

  // --- Mode ---
  virtual bool isPortable() const = 0;

  // --- Root ---
  virtual QString dataDir() const = 0;

  // --- Derived paths ---
  virtual QString profilesDir() const = 0;
  virtual QString settingsFilePath() const = 0;
  virtual QString logsDir() const = 0;
  virtual QString crashesDir() const = 0;
  virtual QString savedDatsDir() const = 0;
  virtual QString savedGfxDir() const = 0;
  virtual QString savedHotkeysDir() const = 0;
  virtual QString markerPacksDir() const = 0;
  virtual QString markerPacksCacheDir() const = 0;
  virtual QString blishModulesDir() const = 0;
  virtual QString radialConfigDir() const = 0;
  virtual QString profileDataDir() const = 0;
  virtual QString markerStateDir() const = 0;
  virtual QString apiCacheDir() const = 0;

  // --- Lifecycle ---
  virtual void ensureDirectories() = 0;
};

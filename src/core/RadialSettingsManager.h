#pragma once

/**
 * @file RadialSettingsManager.h
 * @brief Per-profile radial settings file I/O
 *
 * Manages loading/saving RadialSettings JSON files per profile.
 * Files stored in StorageBackend::radialConfigDir() / {profileId}.json
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialSettingsManager.cpp)
 * - UI code (belongs in RadialTabWidget)
 * - Rendering logic (belongs in RadialController)
 */

#include <QObject>
#include <QString>

#include "RadialSettings.h"

class RadialSettingsManager : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Construct with base directory path
   * @param basePath Directory for radial settings JSON files (from StorageBackend)
   */
  explicit RadialSettingsManager(const QString &basePath,
                                 QObject *parent = nullptr);

  /**
   * @brief Load settings for a specific profile
   * @param profileId Profile UUID
   * @return true if file was loaded (false = new defaults created)
   */
  bool loadForProfile(const QString &profileId);

  /**
   * @brief Save current settings for the loaded profile
   * @return true if write succeeded
   */
  bool saveForProfile(const QString &profileId);

  /**
   * @brief Get current settings (read-only)
   */
  RadialSettings settings() const { return m_settings; }

  /**
   * @brief Replace current settings (emits settingsChanged)
   */
  void setSettings(const RadialSettings &settings);

  /**
   * @brief Reset to factory defaults (emits settingsChanged)
   */
  void resetToDefaults();

  /**
   * @brief Get the currently loaded profile ID
   */
  QString currentProfileId() const { return m_currentProfileId; }

signals:
  /**
   * @brief Emitted when settings are modified via setSettings() or resetToDefaults()
   */
  void settingsChanged();

private:
  QString filePath(const QString &profileId) const;

  QString m_basePath;
  QString m_currentProfileId;
  RadialSettings m_settings;
};

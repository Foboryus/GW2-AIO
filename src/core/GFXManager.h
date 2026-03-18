#pragma once

/**
 * @file GFXManager.h
 * @brief GFX Settings Manager - Per-profile graphics configuration
 *
 * Manages GFXSettings.xml files for per-profile graphics settings.
 * Each profile can have its own saved graphics configuration that
 * is applied before GW2 launches.
 *
 * DO NOT ADD:
 * - Inline implementations (use GFXManager.cpp)
 * - UI code (belongs in GraphicsTabWidget)
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStandardPaths>
#include <QString>

/**
 * @brief All GW2 graphics settings stored in GFXSettings.xml
 */
struct GfxSettings {
  // Display
  QString screenMode = "windowed"; // windowed, fullscreen, windowed_fullscreen
  int width = 1920;
  int height = 1080;
  int refreshRate = 60;
  QString frameLimit = "unlimited"; // unlimited, 30, 60, 120, 144
  double gamma = 1.0;               // 0.5 - 3.5
  bool verticalSync = false;
  bool dpiScaling = false;

  // Quality
  QString shadows = "high";           // off, low, medium, high, ultra
  QString reflections = "all";        // none, terrain, all
  QString textureDetail = "high";     // low, medium, high
  QString shaders = "high";           // low, medium, high
  QString environment = "high";       // low, medium, high
  QString animation = "high";         // low, medium, high
  QString lodDistance = "high";       // low, medium, high, ultra
  QString antiAliasing = "smaa_high"; // none, fxaa, smaa_low, smaa_high
  QString sampling = "native";        // subsample, native, supersample

  // Characters
  QString charModelLimit = "high";   // lowest, low, medium, high, highest
  QString charModelQuality = "high"; // lowest, low, medium, high, highest
  bool highResCharacter = true;
  bool effectLod = false;

  // Advanced
  bool bestTextureFiltering = true;
  bool screenspaceShadows = false;
  QString postProc = "high"; // off, low, medium, high, custom
  bool depthBlur = false;

  // Preset tracking (not in XML, for UI)
  QString presetName = "Custom";
};

/**
 * @brief Manages GFX settings files for per-profile graphics
 */
class GFXManager : public QObject {
  Q_OBJECT

public:
  explicit GFXManager(QObject *parent = nullptr);
  explicit GFXManager(const QString &savedGfxDir, QObject *parent = nullptr);

  // === File Operations ===

  /**
   * @brief Parse a GFXSettings.xml file into struct
   */
  GfxSettings parseGfxFile(const QString &path);

  /**
   * @brief Write settings struct to XML file
   */
  bool writeGfxFile(const QString &path, const GfxSettings &settings);

  /**
   * @brief Apply a profile's GFX settings before launch
   */
  bool applyGfxSettings(const QString &gfxPath);

  // === Presets ===

  /**
   * @brief Get settings for a named preset
   */
  static GfxSettings getPreset(const QString &presetName);

  /**
   * @brief Available preset names
   */
  static QStringList presetNames();

  // === Paths ===

  QString gw2GfxPath() const { return m_gw2GfxPath; }
  QString backupPath() const { return m_backupPath; }
  QString savedGfxPath() const { return m_savedGfxPath; }
  bool hasBackup() const;

signals:
  void applied(const QString &profileName);
  void error(const QString &message);

private:
  bool ensureBackup();

  QString m_gw2GfxPath;
  QString m_backupPath;
  QString m_savedGfxPath;
};

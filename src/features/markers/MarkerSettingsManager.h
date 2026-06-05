#pragma once

/**
 * @brief Per-profile marker preferences manager
 *
 * Stores which marker packs are enabled, category overrides, and overlay
 * display settings per profile. Uses versioned JSON files in markerStateDir.
 *
 * Storage layout (Option D — per-pack-per-profile):
 *   marker_state/{profileId}/{packId}.json   — pack enable + category overrides
 *   marker_state/{profileId}/_display.json   — overlay/minimap opacity
 *
 * Data flow:
 *   StorageBackend::markerStateDir() → DataService → MarkerSettingsManager
 *   MarkerSettingsManager ↔ MarkersTabWidget (Profile Editor)
 *   MarkerSettingsManager → MarkerManager (pack/category filtering)
 *
 * DO NOT ADD:
 * - Marker rendering (belongs in renderers)
 * - Activation tracking (belongs in ActivationStore)
 * - UI code (belongs in widgets)
 */

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include "rendering/ExclusionData.h"

class MarkerSettingsManager : public QObject {
  Q_OBJECT

public:
  explicit MarkerSettingsManager(const QString &stateDir,
                                 QObject *parent = nullptr);
  ~MarkerSettingsManager();

  // --- Profile lifecycle ---

  /**
   * @brief Load settings for a profile (or create defaults)
   * Scans {profileId}/ directory for per-pack JSON files and _display.json.
   * Auto-migrates from old single-file format if detected.
   */
  void loadForProfile(const QString &profileId);

  /**
   * @brief Save all dirty settings immediately
   * @return true if all writes succeeded, false if any write failed
   */
  bool saveNow();

  /**
   * @brief Delete all settings for a profile (cleanup on profile removal)
   * Removes the entire {profileId}/ directory.
   */
  void deleteForProfile(const QString &profileId);

  /**
   * @brief Current profile ID (empty if none loaded)
   */
  QString currentProfileId() const { return m_currentProfileId; }

  // --- Pack settings ---

  /**
   * @brief Check if a pack is enabled (default: true for unknown packs)
   */
  bool isPackEnabled(const QString &packId) const;

  /**
   * @brief Get all pack IDs that have saved enable/disable state
   */
  QList<QString> enabledPackIds() const { return m_packEnabled.keys(); }

  /**
   * @brief Enable or disable a pack
   */
  void setPackEnabled(const QString &packId, bool enabled);

  /**
   * @brief Set all known packs to enabled or disabled (Select All / Clear All)
   * @param packIds List of pack IDs to set
   * @param enabled Whether to enable or disable
   */
  void setAllPacksEnabled(const QStringList &packIds, bool enabled);

  // --- Category overrides (sparse — only exceptions stored) ---

  /**
   * @brief Check if a category is enabled (with ancestor cascade)
   * Returns false if any ancestor has an explicit disable.
   * Used by runtime rendering to determine visibility.
   */
  bool isCategoryEnabled(const QString &packId,
                         const QString &categoryPath) const;

  /**
   * @brief Check a category's own enabled state (no ancestor cascade)
   * Returns only the direct override for this specific category.
   * Used by checkbox UI sync to avoid cascading parent state to children.
   * @return true if enabled or no override exists
   */
  bool isCategoryDirectEnabled(const QString &packId,
                               const QString &categoryPath) const;

  /**
   * @brief Override a category's enabled state
   * @param packId Pack the category belongs to
   * @param categoryPath Category fullName (e.g., "harvest.ore")
   */
  void setCategoryEnabled(const QString &packId, const QString &categoryPath,
                          bool enabled);

  /**
   * @brief Batch set all categories for a pack (single emit + save)
   * @param packId Pack the categories belong to
   * @param categoryPaths List of category fullNames to set
   * @param enabled Whether to enable or disable all
   */
  void setAllCategoriesEnabled(const QString &packId,
                               const QStringList &categoryPaths, bool enabled);

  /**
   * @brief Remove a category override (revert to pack default)
   */
  void clearCategoryOverride(const QString &packId,
                             const QString &categoryPath);

  // --- Overlay display settings ---

  qreal overlayOpacity() const { return m_overlayOpacity; }
  void setOverlayOpacity(qreal opacity);

  qreal minimapOpacity() const { return m_minimapOpacity; }
  void setMinimapOpacity(qreal opacity);

  qreal maxRenderDistance() const { return m_maxRenderDistance; }
  void setMaxRenderDistance(qreal distance);

  bool showDistance() const { return m_showDistance; }
  void setShowDistance(bool show);

  int distanceFontSize() const { return m_distanceFontSize; }
  void setDistanceFontSize(int size);

  qreal markerScale() const { return m_markerScale; }
  void setMarkerScale(qreal scale);

  int distanceLabelOffset() const { return m_distanceLabelOffset; }
  void setDistanceLabelOffset(int offset);

  bool hideInCombat() const { return m_hideInCombat; }
  void setHideInCombat(bool hide);

  bool showInBigMap() const { return m_showInBigMap; }
  void setShowInBigMap(bool show);

  // --- Rendering layer toggles (per-profile, all ON by default) ---

  bool renderingEnabled() const { return m_renderingEnabled; }
  void setRenderingEnabled(bool enabled);

  bool render3dEnabled() const { return m_render3dEnabled; }
  void setRender3dEnabled(bool enabled);

  /// Master gate for map child (minimap + big map). When OFF, map child is
  /// killed. Sub-toggles (renderMinimap/BigMap) are independent within it.
  bool renderMapEnabled() const { return m_renderMapEnabled; }
  void setRenderMapEnabled(bool enabled);

  bool renderMinimapEnabled() const { return m_renderMinimapEnabled; }
  void setRenderMinimapEnabled(bool enabled);

  bool renderBigMapEnabled() const { return m_renderBigMapEnabled; }
  void setRenderBigMapEnabled(bool enabled);

  // --- Height filter settings ---

  bool heightFilterEnabled() const { return m_heightFilterEnabled; }
  void setHeightFilterEnabled(bool enabled);

  float heightFilterRange() const { return m_heightFilterRange; }
  void setHeightFilterRange(float range);

  float minimapTrailWidth() const { return m_minimapTrailWidth; }
  void setMinimapTrailWidth(float width);

  qreal minimapMarkerScale() const { return m_minimapMarkerScale; }
  void setMinimapMarkerScale(qreal scale);

  qreal minimapMarkerOpacity() const { return m_minimapMarkerOpacity; }
  void setMinimapMarkerOpacity(qreal opacity);

  // --- Exclusion zone settings ---

  bool exclusionEnabled() const { return m_exclusionEnabled; }
  void setExclusionEnabled(bool enabled);

  bool minimapZoneEnabled() const { return m_minimapZoneEnabled; }
  void setMinimapZoneEnabled(bool enabled);

  bool skillBarZoneEnabled() const { return m_skillBarZoneEnabled; }
  void setSkillBarZoneEnabled(bool enabled);

  bool chatZoneEnabled() const { return m_chatZoneEnabled; }
  void setChatZoneEnabled(bool enabled);

  float exclusionFadeEdge() const { return m_exclusionFadeEdge; }
  void setExclusionFadeEdge(float edge);

  const QVector<ExclusionZone> &customZones() const { return m_customZones; }
  void setCustomZones(const QVector<ExclusionZone> &zones);
  void addCustomZone(const ExclusionZone &zone);
  void removeCustomZone(int index);

  // --- Predefined zone overrides ---
  // Allow users to reposition chat/skill bar zones from their defaults
  bool hasPredefinedOverride(const QString &name) const;
  ExclusionZone predefinedOverride(const QString &name) const;
  void setPredefinedOverride(const QString &name, const ExclusionZone &zone);
  void resetPredefinedOverride(const QString &name);

  /**
   * @brief Apply rendering toggle fields from an IPC JSON payload
   *
   * Selectively applies rendering layer toggles from a JSON object
   * received via child process IPC (SETTING_CHANGED). Only processes
   * renderingEnabled, render3dEnabled, renderMapEnabled,
   * renderMinimapEnabled, renderBigMapEnabled. Other fields are ignored.
   *
   * Triggers settingsChanged if any value changed (via individual setters).
   */
  void applyDisplayJson(const QJsonObject &obj);

signals:
  void settingsChanged();
  void packEnabledChanged(const QString &packId, bool enabled);
  void saved();  // emitted after saveNow() writes to disk

private:
  void scheduleSave();

  // File path helpers
  QString profileDir(const QString &profileId) const;
  QString packFilePath(const QString &profileId, const QString &packId) const;
  QString displayFilePath(const QString &profileId) const;

  // Per-pack JSON serialization
  QJsonObject packToJson(const QString &packId) const;
  void packFromJson(const QString &packId, const QJsonObject &obj);

  // Display settings JSON serialization
  QJsonObject displayToJson() const;
  void displayFromJson(const QJsonObject &obj);

  void resetToDefaults();

  // Backward migration from old single-file format
  void migrateFromOldFormat(const QString &profileId);

  QString m_stateDir;
  QString m_currentProfileId;

  // Per-pack enabled state (default: true for unknown packs)
  QHash<QString, bool> m_packEnabled;

  // Per-pack category overrides (sparse — only exceptions)
  // Outer key = packId, inner key = category fullName
  QHash<QString, QHash<QString, bool>> m_packCategoryOverrides;

  // Track which packs have been modified since last save
  QSet<QString> m_dirtyPacks;
  bool m_displayDirty = false;

  // Display settings
  qreal m_overlayOpacity = 1.0;
  qreal m_minimapOpacity = 1.0;
  qreal m_maxRenderDistance = 200.0; // Default 200 meters
  bool m_showDistance = false;       // Distance labels below markers
  int m_distanceFontSize = 12;       // Distance label font size (px)
  qreal m_markerScale = 1.0;         // Global marker size multiplier
  int m_distanceLabelOffset = 5;    // Distance label vertical offset (px)
  bool m_hideInCombat = false;       // Hide overlay during combat (default OFF)
  bool m_showInBigMap = true;        // Show overlay in big map even during combat
  bool m_renderingEnabled = true;    // Main kill switch (all rendering)
  bool m_render3dEnabled = true;     // 3D world markers/trails
  bool m_renderMapEnabled = true;    // Master gate for map child (minimap + big map)
  bool m_renderMinimapEnabled = true; // Minimap markers/trails (sub-toggle)
  bool m_renderBigMapEnabled = true;  // Big map (M key) markers/trails (sub-toggle)
  bool m_heightFilterEnabled =
      true; // Height filter ON by default (pre-release)
  float m_heightFilterRange = 20.0f; // Filter range in meters
  float m_minimapTrailWidth = 5.0f;       // Trail width multiplier (1x–10x)
  qreal m_minimapMarkerScale = 1.0;       // Minimap/BigMap marker size (0.5x–3.0x)
  qreal m_minimapMarkerOpacity = 1.0;     // Minimap/BigMap marker opacity

  // Exclusion zone settings
  bool m_exclusionEnabled = true;
  bool m_minimapZoneEnabled = true;
  bool m_skillBarZoneEnabled = true;
  bool m_chatZoneEnabled = true;
  float m_exclusionFadeEdge = 0.02f; // 2% fade at zone edges
  QVector<ExclusionZone> m_customZones;
  QHash<QString, ExclusionZone>
      m_predefinedOverrides; // e.g., "SkillBar", "Chat"

  // Debounced save (2 seconds)
  QTimer *m_saveTimer;
};

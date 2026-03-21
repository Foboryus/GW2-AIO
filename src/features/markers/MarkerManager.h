#pragma once

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>

#include "MarkerModels.h"
#include "TacoParser.h"
#include "core/MumbleLink.h"

class ActivationStore;
class MarkerSettingsManager;

/**
 * @brief Rendering context for visibility filtering
 *
 * TacO/Blish support per-context visibility via:
 * - inGameVisibility  → InGame3D
 * - miniMapVisibility → Minimap
 * - mapVisibility     → BigMap
 */
enum class RenderContext { InGame3D, Minimap, BigMap };

/**
 * @brief Per-instance query context for marker/trail filtering
 *
 * Carries per-instance state (mapId, settings, MumbleLink) so that
 * getVisibleMarkers/getVisibleTrails can filter independently per
 * overlay instance. Shared pack data stays in MarkerManager.
 */
struct MarkerQueryContext {
  uint32_t mapId = 0;                      ///< per-instance MumbleLink::mapId()
  MarkerSettingsManager *settings = nullptr; ///< per-instance settings
  MumbleLink *mumble = nullptr;              ///< per-instance MumbleLink
};

/**
 * @brief Manages loaded marker packs and filtering
 *
 * DO NOT ADD:
 * - Inline implementations (use MarkerManager.cpp)
 */
class MarkerManager : public QObject {
  Q_OBJECT

public:
  explicit MarkerManager(MumbleLink *mumble, QObject *parent = nullptr);

  /// @brief Set the ActivationStore for persistence (owned externally)
  void setActivationStore(ActivationStore *store);

  /// @brief Set the MarkerSettingsManager for persisted pack/category state
  void setMarkerSettings(MarkerSettingsManager *settings);

  /// @brief Get the ActivationStore
  ActivationStore *activationStore() const { return m_activationStore; }

  /**
   * @brief Load all marker packs from a directory
   */
  void loadPacksFromDirectory(const QString &path);

  /**
   * @brief Load all marker packs asynchronously on a background thread
   * Emits packsLoadProgress for each pack, packsLoaded when complete.
   */
  void loadPacksAsync(const QString &path);

  /**
   * @brief Set cache directory for extracted .taco archives
   */
  void setCacheDir(const QString &dir);

  /**
   * @brief Load a single marker pack
   */
  void loadPack(const QString &path, bool metadataOnly = false);

  /**
   * @brief Get all loaded packs
   */
  const QList<MarkerPack> &packs() const { return m_packs; }

  /**
   * @brief Get markers visible on the current map for the given render context
   */
  QList<const Marker *>
  getVisibleMarkers(RenderContext ctx = RenderContext::InGame3D) const;

  /**
   * @brief Get trails visible on the current map for the given render context
   */
  QList<const Trail *>
  getVisibleTrails(RenderContext ctx = RenderContext::InGame3D) const;

  /**
   * @brief Per-instance overloads — use query context instead of shared state
   */
  QList<const Marker *>
  getVisibleMarkers(RenderContext ctx,
                    const MarkerQueryContext &qctx) const;
  QList<const Trail *>
  getVisibleTrails(RenderContext ctx,
                   const MarkerQueryContext &qctx) const;

  /**
   * @brief Toggle category visibility (heavy — updates marker data + emits)
   */
  void setCategoryVisible(const QString &categoryPath, bool visible);

  /**
   * @brief Lightweight visibility update — only updates the category map
   *
   * Safe to call from UI toggles. Does NOT iterate pack markers/trails
   * (getVisibleMarkers() already filters via isCategoryVisible()).
   * Does NOT emit markersChanged() to avoid flooding renderers.
   */
  void updateCategoryVisibility(const QString &categoryPath, bool visible);

  /**
   * @brief Check if category is visible (no pack context)
   */
  bool isCategoryVisible(const QString &categoryPath) const;

  /**
   * @brief Check if category is visible with pack context
   * Falls back to MarkerSettingsManager when no runtime override exists.
   */
  bool isCategoryVisible(const QString &packId,
                         const QString &categoryPath) const;

  /// Per-instance overload — uses query context settings
  bool isCategoryVisible(const QString &packId,
                         const QString &categoryPath,
                         const MarkerQueryContext &qctx) const;

  /**
   * @brief Activate a marker (for behavior handling)
   */
  void activateMarker(const QUuid &guid);

  /**
   * @brief Get current map ID from Mumble
   */
  uint32_t currentMapId() const;

  /**
   * @brief Enable/disable proximity auto-trigger timer
   * Call with false when overlay is hidden to save CPU.
   * Daily reset timer always runs regardless.
   */
  void setProximityEnabled(bool enabled);

  /**
   * @brief Enable/disable height-based floor filtering
   * When enabled, markers/trails with Y far from playerY are hidden.
   */
  void setHeightFilterEnabled(bool enabled);
  void setHeightFilterRange(float range);
  bool heightFilterEnabled() const;
  float heightFilterRange() const;

  /**
   * @brief Restore marker activation state from ActivationStore
   * Called after loadForProfile() to re-hide previously activated markers.
   */
  void restoreActivationState();

  /**
   * @brief Ensure trail point data is loaded for the given map
   *
   * Lazy loading: trail points are not loaded during pack parsing (only
   * the 8-byte header for mapId). This method loads the full .trl point
   * data on demand when the player enters a new map.
   */
  void ensureTrailsLoaded(uint32_t mapId);

  /**
   * @brief Acquire a ref-counted interest in trail data for a map
   *
   * Increments the reference count. On first acquisition, calls
   * ensureTrailsLoaded(). Multiple overlay instances can acquire the
   * same map — trails stay loaded until all release.
   */
  void acquireMap(uint32_t mapId);

  /**
   * @brief Release a ref-counted interest in trail data for a map
   *
   * Decrements the reference count. When it reaches zero, calls
   * unloadTrailPoints(). Defensive: warns on underflow (release
   * without matching acquire), never unloads untracked maps.
   */
  void releaseMap(uint32_t mapId);

signals:
  void packsLoaded();
  void packsLoadProgress(int current, int total, const QString &packName);
  void packsLoadIssues(const QStringList &missingPacks,
                       const QStringList &failedPacks);
  void markersChanged();
  void categoryVisibilityChanged(const QString &path, bool visible);

private slots:
  void onMapChanged(uint32_t mapId);

private:
  void buildCategoryTree();
  void applyCategoryVisibility(MarkerPack &pack);

  // Activation behavior helpers
  void checkProximityActivations();
  void checkDailyReset();
  void resetMarkersForBehavior(MarkerBehavior behavior);
  void resetInstanceMarkers();
  QString activationKey(const Marker &marker) const;

  // Bitmask filter checks against MumbleLink state
  bool passesFilters(uint8_t festival, uint32_t mountFilter,
                     uint32_t profession, uint32_t race,
                     uint32_t specialization) const;
  /// Per-instance overload — uses provided MumbleLink instead of shared m_mumble
  bool passesFilters(uint8_t festival, uint32_t mountFilter,
                     uint32_t profession, uint32_t race,
                     uint32_t specialization,
                     MumbleLink *mumble) const;

  MumbleLink *m_mumble;
  TacoParser *m_parser;
  ActivationStore *m_activationStore = nullptr;
  MarkerSettingsManager *m_markerSettings = nullptr;
  QList<MarkerPack> m_packs;
  QMap<QString, bool> m_categoryVisibility;
  uint32_t m_currentMapId = 0;
  uint32_t m_currentInstance = 0;
  QString m_currentCharName;
  QTimer *m_proximityTimer = nullptr;
  QTimer *m_dailyResetTimer = nullptr;
  bool m_asyncLoading = false;
  QString m_cacheDir;        // For stale cache cleanup
  QStringList m_failedPacks; // Packs that failed to parse

  // Height-based floor filtering
  bool m_heightFilterEnabled = true; // ON by default (pre-release)
  float m_heightFilterRange = 20.0f; // meters above/below player

  // Map pre-filtering index: mapId → list of marker/trail indices per pack
  // Rebuilt when packs are loaded. Avoids O(all_markers) per frame.
  struct MapIndex {
    QHash<uint32_t, QList<int>> markers; // mapId → indices into pack.markers
    QHash<uint32_t, QList<int>> trails;  // mapId → indices into pack.trails
  };
  QList<MapIndex> m_mapIndex; // one per pack, same order as m_packs

  void rebuildMapIndex();

  // Lazy trail loading: load/unload trail points per map
  void unloadTrailPoints(uint32_t mapId);
  QSet<uint32_t> m_loadedTrailMapIds;

  // Ref-counted trail map tracking: mapId → number of active instances
  // Trails stay loaded as long as refCount > 0
  QHash<uint32_t, int> m_trailMapRefCount;
};

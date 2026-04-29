/**
 * @file MarkerManager.cpp
 * @brief Manages loaded marker packs and filtering
 *
 * DO NOT ADD:
 * - Parsing logic (belongs in TacoParser)
 * - Rendering (belongs in MarkerRenderer or GLMarkerRenderer)
 */

#include "MarkerManager.h"
#include "ActivationStore.h"
#include "MarkerSettingsManager.h"
#include "TacoParser.h"

#include <memory>

#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <cmath>

MarkerManager::MarkerManager(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumble(mumble), m_parser(new TacoParser(this)),
      m_proximityTimer(new QTimer(this)), m_dailyResetTimer(new QTimer(this)) {
  if (m_mumble) {
    connect(m_mumble, &MumbleLink::mapChanged, this,
            &MarkerManager::onMapChanged);
  }

  // Proximity check every 500ms for autoTrigger markers
  m_proximityTimer->setInterval(500);
  connect(m_proximityTimer, &QTimer::timeout, this,
          &MarkerManager::checkProximityActivations);
  m_proximityTimer->start();

  // Daily reset check every 60s
  m_dailyResetTimer->setInterval(60000);
  connect(m_dailyResetTimer, &QTimer::timeout, this,
          &MarkerManager::checkDailyReset);
  m_dailyResetTimer->start();
}

void MarkerManager::loadPacksFromDirectory(const QString &path) {
  m_packs.clear(); // Clear existing packs before reloading
  QDir dir(path);

  // Look for .taco files
  for (const QString &file : dir.entryList({"*.taco", "*.zip"}, QDir::Files)) {
    QString packId = QFileInfo(file).completeBaseName();
    bool disabled = m_markerSettings && !m_markerSettings->isPackEnabled(packId);
    if (disabled) {
      qInfo() << "MarkerManager: Loading metadata-only for disabled pack:"
              << packId;
      m_parser->setMetadataOnly(true);
    }
    loadPack(dir.filePath(file), disabled);
    if (disabled) {
      m_parser->setMetadataOnly(false);
    }
  }

  // Look for subdirectories (extracted packs)
  for (const QString &subdir :
       dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QString subdirPath = dir.filePath(subdir);
    QDir packDir(subdirPath);

    // Check if it looks like a marker pack (has XML files)
    if (!packDir.entryList({"*.xml"}, QDir::Files).isEmpty()) {
      QString packId = QFileInfo(subdirPath).completeBaseName();
      bool disabled =
          m_markerSettings && !m_markerSettings->isPackEnabled(packId);
      if (disabled) {
        qInfo() << "MarkerManager: Loading metadata-only for disabled pack:"
                << packId;
        m_parser->setMetadataOnly(true);
      }
      loadPack(subdirPath, disabled);
      if (disabled) {
        m_parser->setMetadataOnly(false);
      }
    }
  }

  buildCategoryTree();
  // Re-process trails with all packs — clear stale set in case onMapChanged
  // already ran with a partial pack list during async loading
  m_loadedTrailMapIds.clear();
  // Reload trails for all maps that have active ref-counts (multibox safety)
  if (!m_trailMapRefCount.isEmpty()) {
    for (auto it = m_trailMapRefCount.constBegin();
         it != m_trailMapRefCount.constEnd(); ++it) {
      ensureTrailsLoaded(it.key());
    }
  } else {
    ensureTrailsLoaded(m_currentMapId);
  }
  emit packsLoaded();
  emit markersChanged(); // Force pipelines to pick up rebuilt index
}

void MarkerManager::loadPacksAsync(const QString &path) {
  m_packs.clear();
  m_failedPacks.clear();

  // Stop timers that read m_packs — prevent data race with worker thread
  m_proximityTimer->stop();
  m_dailyResetTimer->stop();
  m_asyncLoading = true;

  QDir dir(path);

  // Collect all files/dirs to parse (fast — just listing)
  QStringList packPaths;
  for (const QString &file : dir.entryList({"*.taco", "*.zip"}, QDir::Files)) {
    packPaths.append(dir.filePath(file));
  }
  for (const QString &subdir :
       dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QString subdirPath = dir.filePath(subdir);
    QDir packDir(subdirPath);
    if (!packDir.entryList({"*.xml"}, QDir::Files).isEmpty()) {
      packPaths.append(subdirPath);
    }
  }

  // --- 1G: Clean stale cache directories ---
  // Cache dirs are named after .taco/.zip basenames (e.g., MyPack.taco →
  // MarkerPacksCache/MyPack/). Remove any cache dir with no matching source.
  if (!m_cacheDir.isEmpty()) {
    QSet<QString> expectedCacheNames;
    for (const QString &p : packPaths) {
      QFileInfo pfi(p);
      if (pfi.suffix().toLower() == "taco" || pfi.suffix().toLower() == "zip") {
        expectedCacheNames.insert(pfi.completeBaseName());
      }
    }

    QDir cacheRoot(m_cacheDir);
    for (const QString &subdir :
         cacheRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      if (!expectedCacheNames.contains(subdir)) {
        QString stalePath = cacheRoot.filePath(subdir);
        qInfo() << "MarkerManager: removing stale cache dir:" << subdir;
        QDir(stalePath).removeRecursively();
      }
    }
  }

  int total = packPaths.size();
  if (total == 0) {
    m_asyncLoading = false;
    m_proximityTimer->start();
    m_dailyResetTimer->start();
    buildCategoryTree();
    emit packsLoaded();
    return;
  }

  // Snapshot disabled-pack set BEFORE spawning thread (thread-safe read).
  // Avoids reading m_markerSettings from the worker thread.
  QSet<QString> disabledPacks;
  if (m_markerSettings) {
    for (const QString &p : packPaths) {
      QString packId = QFileInfo(p).completeBaseName();
      if (!m_markerSettings->isPackEnabled(packId)) {
        disabledPacks.insert(packId);
      }
    }
  }

  // Thread-local result buffers — written by worker, read by main after finish.
  // Using shared_ptr so the finished handler (main thread) can safely read
  // after the worker thread has completed and been destroyed.
  auto results = std::make_shared<QList<MarkerPack>>();
  auto failures = std::make_shared<QStringList>();
  QString cacheDir = m_cacheDir;

  auto *thread = QThread::create(
      [this, packPaths, total, disabledPacks, cacheDir, results, failures]() {
        // Thread-local parser — completely independent from m_parser.
        // This eliminates the data race where the main-thread m_parser
        // was being called from the worker thread.
        TacoParser localParser(nullptr);
        localParser.setCacheDir(cacheDir);

        for (int i = 0; i < packPaths.size(); ++i) {
          QString packId = QFileInfo(packPaths[i]).completeBaseName();
          bool disabled = disabledPacks.contains(packId);

          if (disabled) {
            qInfo()
                << "MarkerManager: Loading metadata-only for disabled pack:"
                << packId;
            localParser.setMetadataOnly(true);
          }

          MarkerPack pack = localParser.parse(packPaths[i]);

          if (disabled) {
            localParser.setMetadataOnly(false);
            results->append(std::move(pack));
            qInfo() << "Loaded pack metadata:" << results->last().name
                     << "(disabled)";
          } else if (pack.markerCount() > 0 || pack.trailCount() > 0 ||
                     !pack.categories.isEmpty()) {
            qInfo() << "Loaded pack:" << pack.name;
            results->append(std::move(pack));
          } else {
            QString name = QFileInfo(packPaths[i]).completeBaseName();
            qWarning()
                << "MarkerManager: Pack produced 0 markers/trails:" << name;
            failures->append(name);
          }

          emit packsLoadProgress(i + 1, total,
                                 QFileInfo(packPaths[i]).fileName());
        }
      });

  connect(thread, &QThread::finished, this,
          [this, thread, results, failures]() {
            // Merge worker results into member variables (main thread only).
            // Safe: worker thread has fully completed before finished fires.
            m_packs = std::move(*results);
            m_failedPacks = std::move(*failures);

            buildCategoryTree();
            // Re-process trails with all packs — clear stale set in case
            // onMapChanged already ran with a partial pack list during async
            // loading
            m_loadedTrailMapIds.clear();
            // Reload trails for all maps that have active ref-counts (multibox
            // safety)
            if (!m_trailMapRefCount.isEmpty()) {
              for (auto it = m_trailMapRefCount.constBegin();
                   it != m_trailMapRefCount.constEnd(); ++it) {
                ensureTrailsLoaded(it.key());
              }
            } else {
              ensureTrailsLoaded(m_currentMapId);
            }
            m_asyncLoading = false;
            m_proximityTimer->start();
            m_dailyResetTimer->start();
            emit packsLoaded();
            emit markersChanged(); // Force pipelines to pick up rebuilt index

            // Detect missing packs: settings reference packs not on disk
            QStringList missingPacks;
            if (m_markerSettings) {
              QSet<QString> loadedIds;
              for (const auto &pack : m_packs) {
                loadedIds.insert(pack.id);
              }
              // Also count failed packs as "present" (they exist, just broken)
              for (const auto &f : m_failedPacks) {
                loadedIds.insert(f);
              }
              for (const QString &settingsId :
                   m_markerSettings->enabledPackIds()) {
                if (!loadedIds.contains(settingsId)) {
                  missingPacks.append(settingsId);
                }
              }
            }

            if (!missingPacks.isEmpty() || !m_failedPacks.isEmpty()) {
              qInfo() << "MarkerManager: Pack issues —"
                      << missingPacks.size() << "missing,"
                      << m_failedPacks.size() << "failed";
              emit packsLoadIssues(missingPacks, m_failedPacks);
            }

            thread->deleteLater();
          });

  thread->start();
}

void MarkerManager::setCacheDir(const QString &dir) {
  m_cacheDir = dir;
  m_parser->setCacheDir(dir);
}

void MarkerManager::loadPack(const QString &path, bool metadataOnly) {
  MarkerPack pack = m_parser->parse(path);

  // Metadata-only packs (disabled) are always accepted — they may have 0
  // categories if the pack defines category paths only through POI type
  // attributes rather than dedicated MarkerCategory XML elements.
  if (metadataOnly) {
    m_packs.append(pack);
    qInfo() << "Loaded pack metadata:" << pack.name << "(disabled)";
    return;
  }

  // Accept packs that have content
  if (pack.markerCount() > 0 || pack.trailCount() > 0 ||
      !pack.categories.isEmpty()) {
    m_packs.append(pack);
    qInfo() << "Loaded pack:" << pack.name;
  } else {
    // Track failed parses (file exists but produced 0 markers/trails)
    QString name = QFileInfo(path).completeBaseName();
    qWarning() << "MarkerManager: Pack produced 0 markers/trails:" << name;
    m_failedPacks.append(name);
  }
}

void MarkerManager::rebuildMapIndex() {
  m_mapIndex.clear();
  m_mapIndex.reserve(m_packs.size());

  for (int p = 0; p < m_packs.size(); ++p) {
    MapIndex idx;

    // Index markers by mapId
    for (int i = 0; i < m_packs[p].markers.size(); ++i) {
      idx.markers[m_packs[p].markers[i].mapId].append(i);
    }

    // Index trails by mapId
    for (int i = 0; i < m_packs[p].trails.size(); ++i) {
      uint32_t mapId = m_packs[p].trails[i].mapId;
      idx.trails[mapId].append(i);
      // Trails with mapId 0 show on all maps — indexed under 0
    }

    m_mapIndex.append(idx);
  }

  qInfo() << "Map index rebuilt for" << m_packs.size() << "packs";
}

QList<const Marker *>
MarkerManager::getVisibleMarkers(RenderContext ctx) const {
  QList<const Marker *> visible;

  for (int p = 0; p < m_packs.size(); ++p) {
    if (p >= m_mapIndex.size())
      continue;

    // Skip entire pack if disabled in persisted settings
    if (m_markerSettings && !m_markerSettings->isPackEnabled(m_packs[p].id)) {
      continue;
    }

    const MapIndex &idx = m_mapIndex[p];
    const MarkerPack &pack = m_packs[p];

    // Only iterate markers on the current map (O(map_markers) not O(all))
    const QList<int> &indices = idx.markers.value(m_currentMapId);
    for (int i : indices) {
      const Marker &marker = pack.markers[i];

      if (!marker.visible)
        continue;

      // Context-aware visibility filtering
      switch (ctx) {
      case RenderContext::InGame3D:
        if (!marker.inGameVisible)
          continue;
        break;
      case RenderContext::Minimap:
        if (!marker.miniMapVisible)
          continue;
        break;
      case RenderContext::BigMap:
        if (!marker.bigMapVisible)
          continue;
        break;
      }
      if (!isCategoryVisible(pack.id, marker.type))
        continue;
      if (!passesFilters(marker.festival, marker.mountFilter, marker.profession,
                         marker.race, marker.specialization))
        continue;

      // Height-based floor filtering
      {
        bool hfEnabled = m_markerSettings
                             ? m_markerSettings->heightFilterEnabled()
                             : m_heightFilterEnabled;
        float hfRange = m_markerSettings ? m_markerSettings->heightFilterRange()
                                         : m_heightFilterRange;
        if (hfEnabled && m_mumble && m_mumble->isConnected()) {
          float dy = std::abs(marker.ypos - m_mumble->playerY());
          if (dy > hfRange)
            continue;
        }
      }

      visible.append(&marker);
    }
  }

  return visible;
}

QList<const Trail *>
MarkerManager::getVisibleTrails(RenderContext ctx) const {
  QList<const Trail *> visible;

  for (int p = 0; p < m_packs.size(); ++p) {
    if (p >= m_mapIndex.size())
      continue;

    // Skip entire pack if disabled in persisted settings
    if (m_markerSettings && !m_markerSettings->isPackEnabled(m_packs[p].id)) {
      continue;
    }

    const MapIndex &idx = m_mapIndex[p];
    const MarkerPack &pack = m_packs[p];

    // Trails on the current map
    const QList<int> &mapTrails = idx.trails.value(m_currentMapId);
    for (int i : mapTrails) {
      const Trail &trail = pack.trails[i];
      if (!trail.visible)
        continue;

      // Context-aware visibility filtering
      switch (ctx) {
      case RenderContext::InGame3D:
        if (!trail.inGameVisible)
          continue;
        break;
      case RenderContext::Minimap:
        if (!trail.miniMapVisible)
          continue;
        break;
      case RenderContext::BigMap:
        if (!trail.bigMapVisible)
          continue;
        break;
      }
      if (!isCategoryVisible(pack.id, trail.type))
        continue;
      if (!passesFilters(trail.festival, trail.mountFilter, trail.profession,
                         trail.race, trail.specialization))
        continue;

      // Height-based floor filtering for trails:
      // Keep trail if ANY point is within height range (preserves partial
      // routes)
      {
        bool hfEnabled = m_markerSettings
                             ? m_markerSettings->heightFilterEnabled()
                             : m_heightFilterEnabled;
        float hfRange = m_markerSettings ? m_markerSettings->heightFilterRange()
                                         : m_heightFilterRange;
        if (hfEnabled && m_mumble && m_mumble->isConnected() &&
            !trail.points.isEmpty()) {
          float playerY = m_mumble->playerY();
          bool anyInRange = false;
          for (const QVector3D &pt : trail.points) {
            if (std::abs(pt.y() - playerY) <= hfRange) {
              anyInRange = true;
              break;
            }
          }
          if (!anyInRange)
            continue;
        }
      }

      visible.append(&trail);
    }

    // NOTE: mapId=0 trails are NOT included as "global" — many TacO packs
    // store mapId=0 in .trl files (untagged), not intended as "show
    // everywhere". Including them would flood every map with 1000+ unrelated
    // trails.
  }

  return visible;
}

void MarkerManager::setCategoryVisible(const QString &categoryPath,
                                       bool visible) {
  m_categoryVisibility[categoryPath] = visible;

  // Apply to all markers/trails of this category
  for (MarkerPack &pack : m_packs) {
    applyCategoryVisibility(pack);
  }

  emit categoryVisibilityChanged(categoryPath, visible);
  emit markersChanged();
}

void MarkerManager::updateCategoryVisibility(const QString &categoryPath,
                                             bool visible) {
  m_categoryVisibility[categoryPath] = visible;
  // No applyCategoryVisibility() — getVisibleMarkers()/getVisibleTrails()
  // already filter via isCategoryVisible() at read time.
  // No markersChanged() — avoid flooding renderers from UI batch updates.
}

bool MarkerManager::isCategoryVisible(const QString &categoryPath) const {
  // Check each level of the category path
  QStringList parts = categoryPath.split('.');
  QString path;

  for (const QString &part : parts) {
    path = path.isEmpty() ? part : path + "." + part;

    if (m_categoryVisibility.contains(path) && !m_categoryVisibility[path]) {
      return false;
    }
  }

  return m_categoryVisibility.value(categoryPath, true);
}

bool MarkerManager::isCategoryVisible(const QString &packId,
                                      const QString &categoryPath) const {
  // Pack-level master switch — if pack is disabled, all categories are hidden
  if (m_markerSettings && !m_markerSettings->isPackEnabled(packId)) {
    return false;
  }

  // Runtime override takes priority (set by UI toggle during session)
  QStringList parts = categoryPath.split('.');
  QString path;

  for (const QString &part : parts) {
    path = path.isEmpty() ? part : path + "." + part;

    if (m_categoryVisibility.contains(path)) {
      if (!m_categoryVisibility[path]) {
        return false;
      }
      // Explicit runtime override says visible — continue checking children
    }
  }

  // If runtime cache had an explicit entry for the full path, use it
  if (m_categoryVisibility.contains(categoryPath)) {
    return m_categoryVisibility.value(categoryPath);
  }

  // Fall back to persisted settings (single source of truth)
  if (m_markerSettings) {
    return m_markerSettings->isCategoryEnabled(packId, categoryPath);
  }

  return true;
}

// ============================================================================
// Per-instance query context overloads (Phase 7a)
// ============================================================================

bool MarkerManager::isCategoryVisible(
    const QString &packId, const QString &categoryPath,
    const MarkerQueryContext &qctx) const {
  // Pack-level master switch from per-instance settings
  if (qctx.settings && !qctx.settings->isPackEnabled(packId)) {
    return false;
  }

  // Runtime cache is SKIPPED — per-instance uses settings directly.
  // This ensures full independence between instances.
  if (qctx.settings) {
    return qctx.settings->isCategoryEnabled(packId, categoryPath);
  }

  return true;
}

QList<const Marker *>
MarkerManager::getVisibleMarkers(RenderContext ctx,
                                 const MarkerQueryContext &qctx) const {
  QList<const Marker *> visible;

  for (int p = 0; p < m_packs.size(); ++p) {
    if (p >= m_mapIndex.size())
      continue;

    // Skip pack if disabled in per-instance settings
    if (qctx.settings && !qctx.settings->isPackEnabled(m_packs[p].id)) {
      continue;
    }

    const MapIndex &idx = m_mapIndex[p];
    const MarkerPack &pack = m_packs[p];

    // Filter by per-instance mapId
    const QList<int> &indices = idx.markers.value(qctx.mapId);
    for (int i : indices) {
      const Marker &marker = pack.markers[i];

      if (!marker.visible)
        continue;

      // Context-aware visibility filtering
      switch (ctx) {
      case RenderContext::InGame3D:
        if (!marker.inGameVisible)
          continue;
        break;
      case RenderContext::Minimap:
        if (!marker.miniMapVisible)
          continue;
        break;
      case RenderContext::BigMap:
        if (!marker.bigMapVisible)
          continue;
        break;
      }
      if (!isCategoryVisible(pack.id, marker.type, qctx))
        continue;

      // passesFilters using per-instance MumbleLink
      if (qctx.mumble && qctx.mumble->isConnected()) {
        if (!passesFilters(marker.festival, marker.mountFilter,
                           marker.profession, marker.race,
                           marker.specialization, qctx.mumble))
          continue;
      }

      // Height filter using per-instance settings + MumbleLink
      {
        bool hfEnabled = qctx.settings
                             ? qctx.settings->heightFilterEnabled()
                             : m_heightFilterEnabled;
        float hfRange = qctx.settings ? qctx.settings->heightFilterRange()
                                       : m_heightFilterRange;
        if (hfEnabled && qctx.mumble && qctx.mumble->isConnected()) {
          float dy = std::abs(marker.ypos - qctx.mumble->playerY());
          if (dy > hfRange)
            continue;
        }
      }

      visible.append(&marker);
    }
  }

  return visible;
}

QList<const Trail *>
MarkerManager::getVisibleTrails(RenderContext ctx,
                                const MarkerQueryContext &qctx) const {
  QList<const Trail *> visible;

  for (int p = 0; p < m_packs.size(); ++p) {
    if (p >= m_mapIndex.size())
      continue;

    // Skip pack if disabled in per-instance settings
    if (qctx.settings && !qctx.settings->isPackEnabled(m_packs[p].id)) {
      continue;
    }

    const MapIndex &idx = m_mapIndex[p];
    const MarkerPack &pack = m_packs[p];

    // Filter by per-instance mapId
    const QList<int> &mapTrails = idx.trails.value(qctx.mapId);
    for (int i : mapTrails) {
      const Trail &trail = pack.trails[i];
      if (!trail.visible)
        continue;

      // Context-aware visibility filtering
      switch (ctx) {
      case RenderContext::InGame3D:
        if (!trail.inGameVisible)
          continue;
        break;
      case RenderContext::Minimap:
        if (!trail.miniMapVisible)
          continue;
        break;
      case RenderContext::BigMap:
        if (!trail.bigMapVisible)
          continue;
        break;
      }
      if (!isCategoryVisible(pack.id, trail.type, qctx))
        continue;

      // passesFilters using per-instance MumbleLink
      if (qctx.mumble && qctx.mumble->isConnected()) {
        if (!passesFilters(trail.festival, trail.mountFilter,
                           trail.profession, trail.race,
                           trail.specialization, qctx.mumble))
          continue;
      }

      // Height filter using per-instance settings + MumbleLink
      {
        bool hfEnabled = qctx.settings
                             ? qctx.settings->heightFilterEnabled()
                             : m_heightFilterEnabled;
        float hfRange = qctx.settings ? qctx.settings->heightFilterRange()
                                       : m_heightFilterRange;
        if (hfEnabled && qctx.mumble && qctx.mumble->isConnected() &&
            !trail.points.isEmpty()) {
          float playerY = qctx.mumble->playerY();
          bool anyInRange = false;
          for (const QVector3D &pt : trail.points) {
            if (std::abs(pt.y() - playerY) <= hfRange) {
              anyInRange = true;
              break;
            }
          }
          if (!anyInRange)
            continue;
        }
      }

      visible.append(&trail);
    }
  }

  return visible;
}

void MarkerManager::setActivationStore(ActivationStore *store) {
  m_activationStore = store;
}

void MarkerManager::setMarkerSettings(MarkerSettingsManager *settings) {
  m_markerSettings = settings;
}

void MarkerManager::restoreActivationState() {
  if (!m_activationStore) {
    return;
  }

  QDateTime now = QDateTime::currentDateTimeUtc();
  QDate today = now.date();
  int restored = 0;

  for (MarkerPack &pack : m_packs) {
    for (Marker &marker : pack.markers) {
      if (marker.guid.isNull()) {
        continue;
      }

      QString key = activationKey(marker);
      if (!m_activationStore->isActivated(marker.guid,
                                          key.contains('+') ? 1 : 0)) {
        continue; // Not activated in store — leave visible
      }

      // Check behavior to decide if marker should still be hidden
      switch (marker.behavior) {
      case MarkerBehavior::AlwaysVisible:
        // Always visible — ignore activation
        break;

      case MarkerBehavior::OnlyVisibleBeforeActivation:
        // Once activated, permanently hidden
        marker.activated = true;
        marker.visible = false;
        ++restored;
        break;

      case MarkerBehavior::ReappearOnDailyReset:
      case MarkerBehavior::DailyPerChar: {
        // Check if activation was before today's UTC midnight
        QDateTime activTime = m_activationStore->activationTime(marker.guid, 0);
        if (activTime.isValid() && activTime.date() >= today) {
          // Activated today — still hidden
          marker.activated = true;
          marker.visible = false;
          ++restored;
        }
        // else: daily reset has passed — leave visible
        break;
      }

      case MarkerBehavior::ReappearAfterTimer: {
        // Check if timer has expired
        QDateTime activTime = m_activationStore->activationTime(marker.guid, 0);
        if (activTime.isValid() && marker.resetLength > 0) {
          qint64 elapsed = activTime.msecsTo(now);
          if (elapsed < marker.resetLength) {
            // Timer hasn't expired — still hidden
            marker.activated = true;
            marker.visible = false;
            ++restored;
          }
        }
        break;
      }

      default:
        // Map-based/instance-based behaviors:
        // ReappearOnMapChange, ReappearOnMapReset, OncePerInstance,
        // OncePerInstancePerChar, WvWObjective
        // These depend on runtime context (map/instance) which we
        // don't have at startup. Leave visible — they'll be re-evaluated
        // when onMapChanged fires.
        break;
      }
    }
  }

  if (restored > 0) {
    qInfo() << "ActivationStore: Restored" << restored
            << "markers from persisted state";
    emit markersChanged();
  }
}

void MarkerManager::activateMarker(const QUuid &guid) {
  for (MarkerPack &pack : m_packs) {
    for (Marker &marker : pack.markers) {
      if (marker.guid != guid)
        continue;

      marker.activated = true;
      QString key = activationKey(marker);

      // Persist activation
      if (m_activationStore && !key.isEmpty()) {
        m_activationStore->activate(marker.guid, marker.resetLength);
      }

      // Handle behavior
      switch (marker.behavior) {
      case MarkerBehavior::AlwaysVisible:
        // Always visible — activation has no visibility effect
        break;

      case MarkerBehavior::ReappearOnMapChange:
        // Hidden until next map change
        marker.visible = false;
        break;

      case MarkerBehavior::ReappearOnDailyReset:
        // Hidden until UTC midnight
        marker.visible = false;
        break;

      case MarkerBehavior::OnlyVisibleBeforeActivation:
        // Permanently hidden this session
        marker.visible = false;
        break;

      case MarkerBehavior::ReappearAfterTimer:
        // Hidden, reappear after resetLength seconds
        marker.visible = false;
        if (marker.resetLength > 0) {
          QUuid capturedGuid = marker.guid;
          QTimer::singleShot(marker.resetLength * 1000, this,
                             [this, capturedGuid]() {
                               for (MarkerPack &p : m_packs) {
                                 for (Marker &m : p.markers) {
                                   if (m.guid == capturedGuid) {
                                     m.visible = true;
                                     m.activated = false;
                                     emit markersChanged();
                                     return;
                                   }
                                 }
                               }
                             });
        }
        break;

      case MarkerBehavior::ReappearOnMapReset:
        // Hidden until map instance changes (different shard)
        marker.visible = false;
        break;

      case MarkerBehavior::OncePerInstance:
        // Hidden for this instance only
        marker.visible = false;
        break;

      case MarkerBehavior::DailyPerChar:
        // Hidden until UTC midnight, per character
        marker.visible = false;
        break;

      case MarkerBehavior::OncePerInstancePerChar:
        // Hidden for this instance+character combo
        marker.visible = false;
        break;

      case MarkerBehavior::WvWObjective:
        // Hidden (WvW objective tracking)
        marker.visible = false;
        break;
      }

      emit markersChanged();
      return;
    }
  }
}

void MarkerManager::onMapChanged(uint32_t mapId) {
  uint32_t prevMapId = m_currentMapId;
  uint32_t prevInstance = m_currentInstance;
  m_currentMapId = mapId;

  // Update instance and character from MumbleLink
  if (m_mumble) {
    m_currentInstance = m_mumble->mapInstance();
    m_currentCharName = m_mumble->characterName();
  }

  bool instanceChanged = (prevInstance != m_currentInstance);

  for (MarkerPack &pack : m_packs) {
    for (Marker &marker : pack.markers) {
      if (!marker.activated)
        continue;

      switch (marker.behavior) {
      case MarkerBehavior::ReappearOnMapChange:
        // Any map change resets
        marker.visible = true;
        marker.activated = false;
        break;

      case MarkerBehavior::ReappearOnMapReset:
        // Only resets when instance changes
        if (instanceChanged) {
          marker.visible = true;
          marker.activated = false;
        }
        break;

      case MarkerBehavior::OncePerInstance:
        // Resets when instance changes
        if (instanceChanged) {
          marker.visible = true;
          marker.activated = false;
        }
        break;

      case MarkerBehavior::OncePerInstancePerChar:
        // Resets when instance changes
        if (instanceChanged) {
          marker.visible = true;
          marker.activated = false;
        }
        break;

      default:
        break;
      }
    }
  }

  // Lazy trail loading: ref-counted unload/load
  if (prevMapId != 0 && prevMapId != mapId) {
    releaseMap(prevMapId);
  }
  acquireMap(mapId);

  emit markersChanged();
}

void MarkerManager::ensureTrailsLoaded(uint32_t mapId) {
  if (mapId == 0 || m_loadedTrailMapIds.contains(mapId)) {
    return;
  }

  int loaded = 0;
  for (MarkerPack &pack : m_packs) {
    for (Trail &trail : pack.trails) {
      if (trail.mapId == mapId && trail.points.isEmpty() &&
          !trail.trailDataPath.isEmpty()) {
        trail.points = m_parser->parseTrailFile(trail.trailDataPath);
        loaded++;
      }
    }
  }

  m_loadedTrailMapIds.insert(mapId);
  if (loaded > 0) {
    qInfo() << "MarkerManager: Loaded" << loaded
            << "trail point sets for mapId" << mapId;
  }
}

void MarkerManager::unloadTrailPoints(uint32_t mapId) {
  if (mapId == 0 || !m_loadedTrailMapIds.contains(mapId)) {
    return;
  }

  int unloaded = 0;
  for (MarkerPack &pack : m_packs) {
    for (Trail &trail : pack.trails) {
      if (trail.mapId == mapId && !trail.points.isEmpty()) {
        trail.points.clear();
        trail.points.squeeze(); // Release allocated memory
        unloaded++;
      }
    }
  }

  m_loadedTrailMapIds.remove(mapId);
  if (unloaded > 0) {
    qInfo() << "MarkerManager: Unloaded" << unloaded
            << "trail point sets for mapId" << mapId;
  }
}

void MarkerManager::acquireMap(uint32_t mapId) {
  if (mapId == 0)
    return;
  int &count = m_trailMapRefCount[mapId];
  count++;
  qInfo() << "MarkerManager: acquireMap" << mapId << "refCount:" << count;
  ensureTrailsLoaded(mapId);
}

void MarkerManager::releaseMap(uint32_t mapId) {
  if (mapId == 0)
    return;
  auto it = m_trailMapRefCount.find(mapId);
  if (it == m_trailMapRefCount.end()) {
    qWarning() << "MarkerManager: releaseMap called for untracked mapId:"
               << mapId << "— ignoring (defensive guard)";
    return;
  }
  (*it)--;
  qInfo() << "MarkerManager: releaseMap" << mapId << "refCount:" << *it;
  if (*it <= 0) {
    m_trailMapRefCount.erase(it);
    unloadTrailPoints(mapId);
  }
}

void MarkerManager::buildCategoryTree() {
  // NOTE: Do NOT pre-populate m_categoryVisibility here.
  // The runtime cache is for session-level overrides (set by UI toggles).
  // Persisted category state lives in MarkerSettingsManager and is queried
  // by isCategoryVisible(packId, categoryPath) as the fallback.
  // Pre-populating with defaultToggle would shadow persisted overrides.

  // Build map pre-filtering index
  rebuildMapIndex();
}

void MarkerManager::applyCategoryVisibility(MarkerPack &pack) {
  for (Marker &marker : pack.markers) {
    marker.visible = isCategoryVisible(marker.type);
  }
  for (Trail &trail : pack.trails) {
    trail.visible = isCategoryVisible(trail.type);
  }
}

uint32_t MarkerManager::currentMapId() const { return m_currentMapId; }

void MarkerManager::setProximityEnabled(bool enabled) {
  if (enabled && !m_proximityTimer->isActive()) {
    m_proximityTimer->start();
    qInfo() << "Proximity timer resumed";
  } else if (!enabled && m_proximityTimer->isActive()) {
    m_proximityTimer->stop();
    qInfo() << "Proximity timer paused (overlay hidden)";
  }
}

void MarkerManager::checkProximityActivations() {
  if (!m_mumble || !m_mumble->isConnected())
    return;

  QVector3D playerPos = m_mumble->playerPosition();

  for (MarkerPack &pack : m_packs) {
    for (Marker &marker : pack.markers) {
      if (!marker.visible || marker.activated)
        continue;
      if (!marker.autoTrigger)
        continue;
      if (marker.mapId != m_currentMapId)
        continue;

      // Check distance to player
      QVector3D markerPos(marker.xpos, marker.ypos, marker.zpos);
      float dist = (markerPos - playerPos).length();

      if (dist <= marker.triggerRange) {
        activateMarker(marker.guid);
        return; // One activation per tick to avoid cascade
      }
    }
  }
}

void MarkerManager::checkDailyReset() {
  // Check if UTC midnight has passed since any daily-behavior markers
  // were activated. This runs every 60s.
  QDateTime now = QDateTime::currentDateTimeUtc();
  QDate today = now.date();

  bool changed = false;

  for (MarkerPack &pack : m_packs) {
    for (Marker &marker : pack.markers) {
      if (!marker.activated)
        continue;

      bool shouldReset = false;

      if (marker.behavior == MarkerBehavior::ReappearOnDailyReset) {
        shouldReset = true;
      } else if (marker.behavior == MarkerBehavior::DailyPerChar) {
        shouldReset = true;
      }

      if (shouldReset) {
        // Check if activation was before today's UTC midnight
        if (m_activationStore) {
          QDateTime activationTime =
              m_activationStore->activationTime(marker.guid, 0);
          if (activationTime.isValid() && activationTime.date() < today) {
            marker.visible = true;
            marker.activated = false;
            changed = true;
          }
        } else {
          // No store — reset unconditionally on daily check
          marker.visible = true;
          marker.activated = false;
          changed = true;
        }
      }
    }
  }

  if (changed) {
    emit markersChanged();
  }
}

void MarkerManager::resetMarkersForBehavior(MarkerBehavior behavior) {
  bool changed = false;

  for (MarkerPack &pack : m_packs) {
    for (Marker &marker : pack.markers) {
      if (marker.activated && marker.behavior == behavior) {
        marker.visible = true;
        marker.activated = false;
        changed = true;
      }
    }
  }

  if (changed) {
    emit markersChanged();
  }
}

void MarkerManager::resetInstanceMarkers() {
  bool changed = false;

  for (MarkerPack &pack : m_packs) {
    for (Marker &marker : pack.markers) {
      if (!marker.activated)
        continue;

      if (marker.behavior == MarkerBehavior::OncePerInstance ||
          marker.behavior == MarkerBehavior::OncePerInstancePerChar ||
          marker.behavior == MarkerBehavior::ReappearOnMapReset) {
        marker.visible = true;
        marker.activated = false;
        changed = true;
      }
    }
  }

  if (changed) {
    emit markersChanged();
  }
}

QString MarkerManager::activationKey(const Marker &marker) const {
  // TacO uses GUID + uniqueData as the activation key
  // uniqueData can be 0 (global), charHash (per-char), instanceId
  // (per-instance)
  QString guidStr = marker.guid.toString(QUuid::WithoutBraces);

  switch (marker.behavior) {
  case MarkerBehavior::DailyPerChar:
    // Per character: include character name hash
    return guidStr + "+" + QString::number(qHash(m_currentCharName));

  case MarkerBehavior::OncePerInstance:
    // Per instance: include instance ID
    return guidStr + "+" + QString::number(m_currentInstance);

  case MarkerBehavior::OncePerInstancePerChar:
    // Per instance + char: include both
    return guidStr + "+" + QString::number(m_currentInstance) + "+" +
           QString::number(qHash(m_currentCharName));

  default:
    // All other behaviors: just GUID
    return guidStr;
  }
}

bool MarkerManager::passesFilters(uint8_t festival, uint32_t mountFilter,
                                  uint32_t profession, uint32_t race,
                                  uint32_t specialization) const {
  return passesFilters(festival, mountFilter, profession, race,
                       specialization, m_mumble);
}

bool MarkerManager::passesFilters(uint8_t festival, uint32_t mountFilter,
                                  uint32_t profession, uint32_t race,
                                  uint32_t specialization,
                                  MumbleLink *mumble) const {
  if (!mumble || !mumble->isConnected()) {
    // No MumbleLink data — show everything (can't filter without state)
    return true;
  }

  // Mount filter: TacO bitmask where bit N = allow mount index N
  // mountIndex 0 = not mounted
  if (mountFilter != 0) {
    uint32_t mountBit = 1U << mumble->mountIndex();
    if (!(mountFilter & mountBit)) {
      return false;
    }
  }

  // Profession filter: bitmask where bit N = allow profession index N
  // GW2 profession indices: 1-9
  if (profession != 0) {
    uint32_t profBit = 1U << mumble->profession();
    if (!(profession & profBit)) {
      return false;
    }
  }

  // Race filter: bitmask where bit N = allow race index N
  // GW2 race indices: 0-4
  if (race != 0) {
    uint32_t raceBit = 1U << mumble->race();
    if (!(race & raceBit)) {
      return false;
    }
  }

  // Specialization filter: bitmask where bit N = allow spec ID N
  // Note: spec IDs can be large (e.g., 55 for Berserker).
  // TacO uses this as a bitmask, but spec IDs > 31 won't fit in uint32_t.
  // For practical purposes, check if the current spec matches any set bit.
  if (specialization != 0 && mumble->specialization() < 32) {
    uint32_t specBit = 1U << mumble->specialization();
    if (!(specialization & specBit)) {
      return false;
    }
  }

  // Festival filter: TacO bitmask for active festivals
  // REVIEW BEFORE BETA: festival filter not implemented (needs GW2 API)
  // TODO: GW2 doesn't expose current festival via MumbleLink.
  // This requires GW2 API integration (Phase 2.5, item 11c).
  // For now, always pass festival filter.
  (void)festival;

  return true;
}

void MarkerManager::setHeightFilterEnabled(bool enabled) {
  if (m_heightFilterEnabled == enabled)
    return;
  m_heightFilterEnabled = enabled;
  emit markersChanged();
}

void MarkerManager::setHeightFilterRange(float range) {
  if (qFuzzyCompare(m_heightFilterRange, range))
    return;
  m_heightFilterRange = range;
  if (m_heightFilterEnabled) {
    emit markersChanged();
  }
}

bool MarkerManager::heightFilterEnabled() const {
  return m_heightFilterEnabled;
}

float MarkerManager::heightFilterRange() const { return m_heightFilterRange; }

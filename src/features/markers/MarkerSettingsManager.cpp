/**
 * @file MarkerSettingsManager.cpp
 * @brief Per-profile marker preferences persistence
 *
 * Storage layout (Option D — per-pack-per-profile):
 *   marker_state/{profileId}/{packId}.json   — pack enable + category overrides
 *   marker_state/{profileId}/_display.json   — overlay/minimap opacity
 *
 * Uses AtomicFileWriter for crash-safe writes.
 * Debounced save (2 seconds) to avoid thrashing on rapid toggles.
 * Only dirty packs are written on save.
 */

#include "MarkerSettingsManager.h"
#include "core/AtomicFileWriter.h"
#include <QCoreApplication>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

static const int SAVE_DEBOUNCE_MS = 2000;

MarkerSettingsManager::MarkerSettingsManager(const QString &stateDir,
                                             QObject *parent)
    : QObject(parent), m_stateDir(stateDir), m_saveTimer(new QTimer(this)) {
  m_saveTimer->setSingleShot(true);
  m_saveTimer->setInterval(SAVE_DEBOUNCE_MS);
  connect(m_saveTimer, &QTimer::timeout, this, &MarkerSettingsManager::saveNow);
  resetToDefaults();
}

MarkerSettingsManager::~MarkerSettingsManager() {
  // Flush any pending debounced saves before destruction
  if (m_saveTimer->isActive()) {
    qInfo() << "MarkerSettingsManager: Flushing pending saves on shutdown";
    saveNow();
  }
}
// ---------------------------------------------------------------------------
// Profile lifecycle
// ---------------------------------------------------------------------------

void MarkerSettingsManager::loadForProfile(const QString &profileId) {
  // Save current profile if switching
  if (!m_currentProfileId.isEmpty() && m_currentProfileId != profileId) {
    saveNow();
  }

  m_currentProfileId = profileId;
  resetToDefaults();

  qInfo() << "MarkerSettingsManager: Loading settings for profile" << profileId;

  if (profileId.isEmpty()) {
    qInfo() << "MarkerSettingsManager: No profile, using defaults";
    return;
  }

  // Check for old single-file format and migrate if needed
  migrateFromOldFormat(profileId);

  // Load display settings
  QString displayPath = displayFilePath(profileId);
  if (QFile::exists(displayPath)) {
    QFile file(displayPath);
    if (file.open(QIODevice::ReadOnly)) {
      QJsonParseError parseError;
      QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
      file.close();

      if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        displayFromJson(doc.object());
      } else {
        qWarning() << "MarkerSettingsManager: Bad _display.json:"
                   << parseError.errorString();
      }
    }
  }

  // Load per-pack settings by scanning the profile directory
  QDir profDir(profileDir(profileId));
  if (!profDir.exists()) {
    qInfo() << "MarkerSettingsManager: No settings dir for profile" << profileId
            << "— using defaults";
    return;
  }

  QStringList jsonFiles =
      profDir.entryList({"*.json"}, QDir::Files, QDir::Name);

  for (const QString &fileName : jsonFiles) {
    // Skip the display settings file
    if (fileName == "_display.json") {
      continue;
    }

    // Pack ID = filename without .json extension
    QString packId = fileName.chopped(5); // Remove ".json"

    QFile file(profDir.filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
      qWarning() << "MarkerSettingsManager: Failed to open" << fileName;
      continue;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
      qWarning() << "MarkerSettingsManager: Bad JSON in" << fileName << "—"
                 << parseError.errorString();
      continue;
    }

    if (!doc.isObject()) {
      continue;
    }

    QJsonObject obj = doc.object();

    // Validate type field
    QString type = obj["type"].toString();
    if (!type.isEmpty() && type != "markerPackSettings") {
      qWarning() << "MarkerSettingsManager: Wrong type" << type << "in"
                 << fileName;
      continue;
    }

    packFromJson(packId, obj);
  }

  qInfo() << "MarkerSettingsManager: Loaded settings for profile" << profileId
          << "—" << m_packEnabled.size() << "packs,"
          << m_packCategoryOverrides.size() << "packs with overrides";

  // Notify listeners (overlay menu, pack browser) that settings changed
  emit settingsChanged();
}

bool MarkerSettingsManager::saveNow() {
  if (m_currentProfileId.isEmpty()) {
    qInfo() << "MarkerSettingsManager::saveNow() — no profile loaded, skipping";
    return true; // Nothing to save — vacuously successful
  }

  m_saveTimer->stop();
  bool allOk = true;

  // Ensure profile directory exists
  QString profPath = profileDir(m_currentProfileId);
  QDir().mkpath(profPath);
  qInfo() << "MarkerSettingsManager::saveNow() — profile:" << m_currentProfileId
          << "dir:" << profPath << "displayDirty:" << m_displayDirty
          << "dirtyPacks:" << m_dirtyPacks.size();

  // Save only dirty packs
  for (const QString &packId : m_dirtyPacks) {
    QString filePath = packFilePath(m_currentProfileId, packId);
    QJsonObject obj = packToJson(packId);

    if (AtomicFileWriter::writeJson(filePath, obj)) {
      qDebug() << "MarkerSettingsManager: Saved pack settings" << packId; // DEV LOG — remove before release
    } else {
      qWarning() << "MarkerSettingsManager: Failed to save" << filePath;
      allOk = false;
    }
  }
  m_dirtyPacks.clear();

  // Save display settings if dirty
  if (m_displayDirty) {
    QString filePath = displayFilePath(m_currentProfileId);
    QJsonObject obj = displayToJson();

    if (AtomicFileWriter::writeJson(filePath, obj)) {
      qDebug() << "MarkerSettingsManager: Saved display settings"; // DEV LOG — remove before release
    } else {
      qWarning() << "MarkerSettingsManager: Failed to save display settings";
      allOk = false;
    }
    m_displayDirty = false;
  }

  emit saved();
  return allOk;
}

void MarkerSettingsManager::deleteForProfile(const QString &profileId) {
  if (profileId.isEmpty()) {
    return;
  }

  // Remove entire profile directory
  QString profPath = profileDir(profileId);
  QDir profDir(profPath);
  if (profDir.exists()) {
    profDir.removeRecursively();
    qInfo() << "MarkerSettingsManager: Deleted settings directory for"
            << profileId;
  }

  // Clear state if this was the active profile
  if (m_currentProfileId == profileId) {
    m_currentProfileId.clear();
    resetToDefaults();
  }
}

// ---------------------------------------------------------------------------
// Pack settings
// ---------------------------------------------------------------------------

bool MarkerSettingsManager::isPackEnabled(const QString &packId) const {
  return m_packEnabled.value(packId, true); // default: enabled
}

void MarkerSettingsManager::setPackEnabled(const QString &packId,
                                           bool enabled) {
  if (m_packEnabled.value(packId, true) == enabled) {
    return; // No change
  }
  m_packEnabled[packId] = enabled;

  // Category overrides are preserved across pack enable/disable.
  // Pack state acts as a master switch at runtime, not a reset.

  m_dirtyPacks.insert(packId);
  scheduleSave();
  emit settingsChanged();
  emit packEnabledChanged(packId, enabled);
}

void MarkerSettingsManager::setAllPacksEnabled(const QStringList &packIds,
                                               bool enabled) {
  bool anyChanged = false;

  for (const QString &packId : packIds) {
    if (m_packEnabled.value(packId, true) != enabled) {
      m_packEnabled[packId] = enabled;
      m_dirtyPacks.insert(packId);
      anyChanged = true;
    }
  }

  if (anyChanged) {
    scheduleSave();
    emit settingsChanged();
  }
}

// ---------------------------------------------------------------------------
// Category overrides
// ---------------------------------------------------------------------------

bool MarkerSettingsManager::isCategoryEnabled(
    const QString &packId, const QString &categoryPath) const {
  // Check for explicit overrides — walk each ancestor level
  auto packIt = m_packCategoryOverrides.find(packId);
  if (packIt != m_packCategoryOverrides.end()) {
    QStringList parts = categoryPath.split('.');
    QString path;

    for (const QString &part : parts) {
      path = path.isEmpty() ? part : path + "." + part;

      auto catIt = packIt->find(path);
      if (catIt != packIt->end() && !catIt.value()) {
        return false;
      }
    }

    // Check exact path for explicit enable
    auto catIt = packIt->find(categoryPath);
    if (catIt != packIt->end()) {
      return catIt.value();
    }
  }

  // No explicit override — category is enabled by default
  return true;
}

bool MarkerSettingsManager::isCategoryDirectEnabled(
    const QString &packId, const QString &categoryPath) const {
  // Direct lookup only — no ancestor walk.
  // Used by checkbox UI sync to avoid cascading parent state to children.
  auto packIt = m_packCategoryOverrides.find(packId);
  if (packIt != m_packCategoryOverrides.end()) {
    auto catIt = packIt->find(categoryPath);
    if (catIt != packIt->end()) {
      return catIt.value();
    }
  }
  return true; // No override → enabled by default
}

void MarkerSettingsManager::setCategoryEnabled(const QString &packId,
                                               const QString &categoryPath,
                                               bool enabled) {
  auto &catOverrides = m_packCategoryOverrides[packId];
  if (catOverrides.value(categoryPath) == enabled &&
      catOverrides.contains(categoryPath)) {
    return; // No change
  }
  catOverrides[categoryPath] = enabled;

  // Category overrides are independent — enabling a parent does NOT
  // clear children's overrides. Each checkbox controls its own state.

  m_dirtyPacks.insert(packId);
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setAllCategoriesEnabled(
    const QString &packId, const QStringList &categoryPaths, bool enabled) {
  auto &catOverrides = m_packCategoryOverrides[packId];
  bool changed = false;
  for (const QString &path : categoryPaths) {
    if (catOverrides.value(path) != enabled || !catOverrides.contains(path)) {
      catOverrides[path] = enabled;
      changed = true;
    }
  }
  if (changed) {
    m_dirtyPacks.insert(packId);
    scheduleSave();
    emit settingsChanged();
  }
}

void MarkerSettingsManager::clearCategoryOverride(const QString &packId,
                                                  const QString &categoryPath) {
  auto it = m_packCategoryOverrides.find(packId);
  if (it == m_packCategoryOverrides.end()) {
    return;
  }
  if (!it->contains(categoryPath)) {
    return;
  }
  it->remove(categoryPath);
  // Clean up empty hash
  if (it->isEmpty()) {
    m_packCategoryOverrides.erase(it);
  }
  m_dirtyPacks.insert(packId);
  scheduleSave();
  emit settingsChanged();
}

// ---------------------------------------------------------------------------
// Overlay display settings
// ---------------------------------------------------------------------------

void MarkerSettingsManager::setOverlayOpacity(qreal opacity) {
  opacity = qBound(0.0, opacity, 1.0);
  if (qFuzzyCompare(m_overlayOpacity, opacity)) {
    return;
  }
  m_overlayOpacity = opacity;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setMinimapOpacity(qreal opacity) {
  opacity = qBound(0.0, opacity, 1.0);
  if (qFuzzyCompare(m_minimapOpacity, opacity)) {
    return;
  }
  m_minimapOpacity = opacity;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setMaxRenderDistance(qreal distance) {
  distance = qBound(50.0, distance, 500.0);
  if (qFuzzyCompare(m_maxRenderDistance, distance)) {
    return;
  }
  m_maxRenderDistance = distance;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setShowDistance(bool show) {
  if (m_showDistance == show) {
    return;
  }
  m_showDistance = show;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setDistanceFontSize(int size) {
  size = qBound(8, size, 24);
  if (m_distanceFontSize == size) {
    return;
  }
  m_distanceFontSize = size;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setMarkerScale(qreal scale) {
  scale = qBound(0.5, scale, 3.0);
  if (qFuzzyCompare(m_markerScale, scale)) {
    return;
  }
  m_markerScale = scale;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setDistanceLabelOffset(int offset) {
  offset = qBound(0, offset, 50);
  if (m_distanceLabelOffset == offset) {
    return;
  }
  m_distanceLabelOffset = offset;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setHideInCombat(bool hide) {
  if (m_hideInCombat == hide) {
    return;
  }
  m_hideInCombat = hide;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setShowInBigMap(bool show) {
  if (m_showInBigMap == show) {
    return;
  }
  m_showInBigMap = show;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setRenderingEnabled(bool enabled) {
  if (m_renderingEnabled == enabled)
    return;
  m_renderingEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setRender3dEnabled(bool enabled) {
  if (m_render3dEnabled == enabled)
    return;
  m_render3dEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setRenderMinimapEnabled(bool enabled) {
  if (m_renderMinimapEnabled == enabled)
    return;
  m_renderMinimapEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setRenderBigMapEnabled(bool enabled) {
  if (m_renderBigMapEnabled == enabled)
    return;
  m_renderBigMapEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setHeightFilterEnabled(bool enabled) {
  if (m_heightFilterEnabled == enabled)
    return;
  m_heightFilterEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setHeightFilterRange(float range) {
  range = qBound(5.0f, range, 50.0f);
  if (qFuzzyCompare(m_heightFilterRange, range))
    return;
  m_heightFilterRange = range;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setMinimapTrailWidth(float width) {
  width = qBound(1.0f, width, 10.0f);
  if (qFuzzyCompare(m_minimapTrailWidth, width))
    return;
  m_minimapTrailWidth = width;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setMinimapMarkerScale(qreal scale) {
  scale = qBound(0.5, scale, 3.0);
  if (qFuzzyCompare(m_minimapMarkerScale, scale))
    return;
  m_minimapMarkerScale = scale;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setMinimapMarkerOpacity(qreal opacity) {
  opacity = qBound(0.0, opacity, 1.0);
  if (qFuzzyCompare(m_minimapMarkerOpacity, opacity))
    return;
  m_minimapMarkerOpacity = opacity;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setExclusionEnabled(bool enabled) {
  if (m_exclusionEnabled == enabled)
    return;
  m_exclusionEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setMinimapZoneEnabled(bool enabled) {
  if (m_minimapZoneEnabled == enabled)
    return;
  m_minimapZoneEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setSkillBarZoneEnabled(bool enabled) {
  if (m_skillBarZoneEnabled == enabled)
    return;
  m_skillBarZoneEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setChatZoneEnabled(bool enabled) {
  if (m_chatZoneEnabled == enabled)
    return;
  m_chatZoneEnabled = enabled;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setExclusionFadeEdge(float edge) {
  edge = qBound(0.0f, edge, 0.05f);
  if (qFuzzyCompare(m_exclusionFadeEdge, edge))
    return;
  m_exclusionFadeEdge = edge;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::setCustomZones(
    const QVector<ExclusionZone> &zones) {
  m_customZones = zones;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::addCustomZone(const ExclusionZone &zone) {
  if (m_customZones.size() >= 5)
    return; // Max 5 custom zones
  m_customZones.append(zone);
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::removeCustomZone(int index) {
  if (index < 0 || index >= m_customZones.size())
    return;
  m_customZones.removeAt(index);
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

bool MarkerSettingsManager::hasPredefinedOverride(const QString &name) const {
  return m_predefinedOverrides.contains(name);
}

ExclusionZone
MarkerSettingsManager::predefinedOverride(const QString &name) const {
  return m_predefinedOverrides.value(name);
}

void MarkerSettingsManager::setPredefinedOverride(const QString &name,
                                                  const ExclusionZone &zone) {
  m_predefinedOverrides[name] = zone;
  m_displayDirty = true;
  scheduleSave();
  emit settingsChanged();
}

void MarkerSettingsManager::resetPredefinedOverride(const QString &name) {
  if (m_predefinedOverrides.remove(name)) {
    m_displayDirty = true;
    scheduleSave();
    emit settingsChanged();
  }
}

// ---------------------------------------------------------------------------
// Private — file paths
// ---------------------------------------------------------------------------

void MarkerSettingsManager::scheduleSave() {
  if (!m_currentProfileId.isEmpty()) {
    m_saveTimer->start(); // Resets the 2-second timer
  }
}

QString MarkerSettingsManager::profileDir(const QString &profileId) const {
  return QDir(m_stateDir).filePath(profileId);
}

QString MarkerSettingsManager::packFilePath(const QString &profileId,
                                            const QString &packId) const {
  return QDir(profileDir(profileId)).filePath(packId + ".json");
}

QString MarkerSettingsManager::displayFilePath(const QString &profileId) const {
  return QDir(profileDir(profileId)).filePath("_display.json");
}

// ---------------------------------------------------------------------------
// Private — per-pack JSON serialization
// ---------------------------------------------------------------------------

QJsonObject MarkerSettingsManager::packToJson(const QString &packId) const {
  QJsonObject obj;
  obj["type"] = "markerPackSettings";
  obj["version"] = 1;
  obj["enabled"] = isPackEnabled(packId);

  // Category overrides (sparse)
  auto it = m_packCategoryOverrides.find(packId);
  if (it != m_packCategoryOverrides.end() && !it->isEmpty()) {
    QJsonObject catsObj;
    for (auto catIt = it->constBegin(); catIt != it->constEnd(); ++catIt) {
      catsObj[catIt.key()] = catIt.value();
    }
    obj["categoryOverrides"] = catsObj;
  }

  return obj;
}

void MarkerSettingsManager::packFromJson(const QString &packId,
                                         const QJsonObject &obj) {
  // Version check (forward-compat)
  int version = obj["version"].toInt(1);
  Q_UNUSED(version);

  // Pack enabled state
  m_packEnabled[packId] = obj["enabled"].toBool(true);

  // Category overrides
  QJsonObject catsObj = obj["categoryOverrides"].toObject();
  if (!catsObj.isEmpty()) {
    QHash<QString, bool> overrides;
    for (auto it = catsObj.constBegin(); it != catsObj.constEnd(); ++it) {
      overrides[it.key()] = it.value().toBool();
    }
    m_packCategoryOverrides[packId] = overrides;
  }
}

// ---------------------------------------------------------------------------
// Private — display settings JSON
// ---------------------------------------------------------------------------

QJsonObject MarkerSettingsManager::displayToJson() const {
  QJsonObject obj;
  obj["type"] = "markerDisplaySettings";
  obj["version"] = 7;
  obj["overlayOpacity"] = m_overlayOpacity;
  obj["minimapOpacity"] = m_minimapOpacity;
  obj["maxRenderDistance"] = m_maxRenderDistance;
  obj["showDistance"] = m_showDistance;
  obj["distanceFontSize"] = m_distanceFontSize;
  obj["markerScale"] = m_markerScale;
  obj["distanceLabelOffset"] = m_distanceLabelOffset;
  obj["hideInCombat"] = m_hideInCombat;
  obj["showInBigMap"] = m_showInBigMap;
  obj["heightFilterEnabled"] = m_heightFilterEnabled;
  obj["heightFilterRange"] = static_cast<double>(m_heightFilterRange);
  obj["minimapTrailWidth"] = static_cast<double>(m_minimapTrailWidth);
  obj["minimapMarkerScale"] = m_minimapMarkerScale;
  obj["minimapMarkerOpacity"] = m_minimapMarkerOpacity;

  // Rendering layer toggles (v7)
  obj["renderingEnabled"] = m_renderingEnabled;
  obj["render3dEnabled"] = m_render3dEnabled;
  obj["renderMinimapEnabled"] = m_renderMinimapEnabled;
  obj["renderBigMapEnabled"] = m_renderBigMapEnabled;

  // Exclusion zones (v3)
  QJsonObject exObj;
  exObj["enabled"] = m_exclusionEnabled;
  exObj["minimapEnabled"] = m_minimapZoneEnabled;
  exObj["skillBarEnabled"] = m_skillBarZoneEnabled;
  exObj["chatEnabled"] = m_chatZoneEnabled;
  exObj["fadeEdge"] = static_cast<double>(m_exclusionFadeEdge);

  QJsonArray customArr;
  for (const auto &zone : m_customZones) {
    QJsonObject z;
    z["name"] = zone.name;
    z["x"] = static_cast<double>(zone.x);
    z["y"] = static_cast<double>(zone.y);
    z["w"] = static_cast<double>(zone.w);
    z["h"] = static_cast<double>(zone.h);
    customArr.append(z);
  }
  exObj["customZones"] = customArr;

  // Predefined zone position overrides
  if (!m_predefinedOverrides.isEmpty()) {
    QJsonObject overrides;
    for (auto it = m_predefinedOverrides.constBegin();
         it != m_predefinedOverrides.constEnd(); ++it) {
      QJsonObject z;
      z["x"] = static_cast<double>(it.value().x);
      z["y"] = static_cast<double>(it.value().y);
      z["w"] = static_cast<double>(it.value().w);
      z["h"] = static_cast<double>(it.value().h);
      overrides[it.key()] = z;
    }
    exObj["predefinedOverrides"] = overrides;
  }

  obj["exclusionZones"] = exObj;
  return obj;
}

void MarkerSettingsManager::displayFromJson(const QJsonObject &obj) {
  m_overlayOpacity = qBound(0.0, obj["overlayOpacity"].toDouble(1.0), 1.0);
  m_minimapOpacity = qBound(0.0, obj["minimapOpacity"].toDouble(0.8), 1.0);
  m_maxRenderDistance =
      qBound(50.0, obj["maxRenderDistance"].toDouble(200.0), 500.0);
  m_showDistance = obj["showDistance"].toBool(false);
  m_distanceFontSize = qBound(8, obj["distanceFontSize"].toInt(12), 24);
  m_markerScale = qBound(0.5, obj["markerScale"].toDouble(1.0), 3.0);
  m_distanceLabelOffset = qBound(0, obj["distanceLabelOffset"].toInt(5), 50);
  m_hideInCombat = obj["hideInCombat"].toBool(false);
  m_showInBigMap = obj["showInBigMap"].toBool(true);
  m_heightFilterEnabled = obj["heightFilterEnabled"].toBool(true);
  m_heightFilterRange = qBound(
      5.0f, static_cast<float>(obj["heightFilterRange"].toDouble(20.0)), 50.0f);
  m_minimapTrailWidth = qBound(
      1.0f, static_cast<float>(obj["minimapTrailWidth"].toDouble(1.0)), 10.0f);
  m_minimapMarkerScale =
      qBound(0.5, obj["minimapMarkerScale"].toDouble(1.0), 3.0);
  m_minimapMarkerOpacity =
      qBound(0.0, obj["minimapMarkerOpacity"].toDouble(1.0), 1.0);

  // Rendering layer toggles (v7 — backward-compat: default true)
  m_renderingEnabled = obj["renderingEnabled"].toBool(true);
  m_render3dEnabled = obj["render3dEnabled"].toBool(true);
  m_renderMinimapEnabled = obj["renderMinimapEnabled"].toBool(true);
  m_renderBigMapEnabled = obj["renderBigMapEnabled"].toBool(true);

  // Exclusion zones (v3 migration — defaults if absent)
  if (obj.contains("exclusionZones")) {
    QJsonObject exObj = obj["exclusionZones"].toObject();
    m_exclusionEnabled = exObj["enabled"].toBool(true);
    m_minimapZoneEnabled = exObj["minimapEnabled"].toBool(true);
    m_skillBarZoneEnabled = exObj["skillBarEnabled"].toBool(true);
    m_chatZoneEnabled = exObj["chatEnabled"].toBool(true);
    m_exclusionFadeEdge = qBound(
        0.0f, static_cast<float>(exObj["fadeEdge"].toDouble(0.02)), 0.05f);

    m_customZones.clear();
    QJsonArray customArr = exObj["customZones"].toArray();
    for (const auto &val : customArr) {
      QJsonObject z = val.toObject();
      ExclusionZone zone;
      zone.name = z["name"].toString();
      zone.x = static_cast<float>(z["x"].toDouble());
      zone.y = static_cast<float>(z["y"].toDouble());
      zone.w = static_cast<float>(z["w"].toDouble());
      zone.h = static_cast<float>(z["h"].toDouble());
      m_customZones.append(zone);
    }

    // Predefined zone overrides (v3)
    m_predefinedOverrides.clear();
    if (exObj.contains("predefinedOverrides")) {
      QJsonObject overrides = exObj["predefinedOverrides"].toObject();
      for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        QJsonObject z = it.value().toObject();
        ExclusionZone zone;
        zone.name = it.key();
        zone.x = static_cast<float>(z["x"].toDouble());
        zone.y = static_cast<float>(z["y"].toDouble());
        zone.w = static_cast<float>(z["w"].toDouble());
        zone.h = static_cast<float>(z["h"].toDouble());
        m_predefinedOverrides[it.key()] = zone;
      }
    }
  }
}

void MarkerSettingsManager::resetToDefaults() {
  m_packEnabled.clear();
  m_packCategoryOverrides.clear();
  m_dirtyPacks.clear();
  m_displayDirty = false;
  m_overlayOpacity = 1.0;
  m_minimapOpacity = 1.0;
  m_maxRenderDistance = 200.0;
  m_showDistance = false;
  m_distanceFontSize = 12;
  m_markerScale = 1.0;
  m_distanceLabelOffset = 5;
  m_hideInCombat = false;
  m_showInBigMap = true;
  m_heightFilterEnabled = true;
  m_heightFilterRange = 20.0f;
  m_minimapTrailWidth = 5.0f;
  m_minimapMarkerScale = 1.0;
  m_minimapMarkerOpacity = 1.0;
  m_renderingEnabled = true;
  m_render3dEnabled = true;
  m_renderMinimapEnabled = true;
  m_renderBigMapEnabled = true;
  m_exclusionEnabled = true;
  m_minimapZoneEnabled = true;
  m_skillBarZoneEnabled = true;
  m_chatZoneEnabled = true;
  m_exclusionFadeEdge = 0.02f;
  m_customZones.clear();
  m_predefinedOverrides.clear();
}

// ---------------------------------------------------------------------------
// Backward migration from old single-file format
// ---------------------------------------------------------------------------

void MarkerSettingsManager::migrateFromOldFormat(const QString &profileId) {
  // Check for old-style settings_{profileId}.json
  QString oldPath =
      QDir(m_stateDir)
          .filePath(QStringLiteral("settings_%1.json").arg(profileId));

  if (!QFile::exists(oldPath)) {
    return; // No old file to migrate
  }

  qInfo() << "MarkerSettingsManager: Migrating old format for profile"
          << profileId;

  QFile file(oldPath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "MarkerSettingsManager: Failed to open old file" << oldPath;
    return;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    qWarning() << "MarkerSettingsManager: Bad old settings file — skipping";
    return;
  }

  QJsonObject obj = doc.object();

  // Read display settings
  m_overlayOpacity = qBound(0.0, obj["overlayOpacity"].toDouble(1.0), 1.0);
  m_minimapOpacity = qBound(0.0, obj["minimapOpacity"].toDouble(0.8), 1.0);
  m_displayDirty = true;

  // Read pack enabled states
  QJsonObject packsObj = obj["enabledPacks"].toObject();
  for (auto it = packsObj.constBegin(); it != packsObj.constEnd(); ++it) {
    m_packEnabled[it.key()] = it.value().toBool(true);
    m_dirtyPacks.insert(it.key());
  }

  // Read category overrides — old format used "packId/category" prefix
  QJsonObject catsObj = obj["categoryOverrides"].toObject();
  for (auto it = catsObj.constBegin(); it != catsObj.constEnd(); ++it) {
    QString fullKey = it.key();
    int slashPos = fullKey.indexOf('/');

    if (slashPos > 0) {
      // Split "packId/category.path" into packId + categoryPath
      QString packId = fullKey.left(slashPos);
      QString catPath = fullKey.mid(slashPos + 1);
      m_packCategoryOverrides[packId][catPath] = it.value().toBool();
      m_dirtyPacks.insert(packId);
    }
    // Entries without "/" prefix are ambiguous — skip them
  }

  // Save migrated data as new per-pack files
  bool saveOk = saveNow();

  if (saveOk) {
    // All new files written successfully — safe to remove old file
    QFile::remove(oldPath);
    QFile::remove(oldPath + ".bak");
    qInfo() << "MarkerSettingsManager: Migration complete —"
            << m_packEnabled.size() << "packs migrated";
  } else {
    // Some writes failed — keep old file as safety net
    qWarning()
        << "MarkerSettingsManager: Migration writes incomplete — keeping old"
        << oldPath << "for safety";
  }
}

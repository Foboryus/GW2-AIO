/**
 * @file ActivationStore.cpp
 * @brief Per-profile marker activation state and category visibility
 * persistence
 *
 * JSON format (versioned):
 *   { "type": "marker_state", "version": 1,
 *     "categoryVisibility": { ... },
 *     "activations": { "<key>": { "lastActivated": "...", "uniqueData": N } }
 *   }
 *
 * DO NOT ADD:
 * - Marker management logic (belongs in MarkerManager)
 * - Path resolution (receives path via constructor)
 */

#include "ActivationStore.h"
#include "core/AtomicFileWriter.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

ActivationStore::ActivationStore(const QString &stateDir, QObject *parent)
    : QObject(parent), m_stateDir(stateDir), m_saveTimer(new QTimer(this)) {
  // Debounce: 500ms delay to batch rapid toggles
  m_saveTimer->setSingleShot(true);
  m_saveTimer->setInterval(500);
  connect(m_saveTimer, &QTimer::timeout, this, &ActivationStore::saveNow);
}

void ActivationStore::loadForProfile(const QString &profileId) {
  // Save any pending state from previous profile
  if (m_saveTimer->isActive()) {
    saveNow();
  }

  m_currentProfileId = profileId;
  m_currentFilePath = QDir(m_stateDir).filePath(profileId + ".json");

  m_categoryVisibility.clear();
  m_activations.clear();

  QFile file(m_currentFilePath);
  if (!file.exists()) {
    qInfo() << "ActivationStore: No state file for profile" << profileId;
    return;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "ActivationStore: Failed to open" << m_currentFilePath;
    return;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "ActivationStore: JSON parse error:"
               << parseError.errorString();
    return;
  }

  QJsonObject root = doc.object();

  // Validate type and version
  if (root.value("type").toString() != "marker_state") {
    qWarning() << "ActivationStore: Unknown file type";
    return;
  }
  int version = root.value("version").toInt(0);
  if (version < 1) {
    qWarning() << "ActivationStore: Unknown version" << version;
    return;
  }

  // Load category visibility
  QJsonObject catVis = root.value("categoryVisibility").toObject();
  for (auto it = catVis.begin(); it != catVis.end(); ++it) {
    m_categoryVisibility.insert(it.key(), it.value().toBool(true));
  }

  // Load activations
  QJsonObject activations = root.value("activations").toObject();
  for (auto it = activations.begin(); it != activations.end(); ++it) {
    QJsonObject entry = it.value().toObject();
    ActivationEntry ae;
    ae.lastActivated = QDateTime::fromString(
        entry.value("lastActivated").toString(), Qt::ISODate);
    ae.uniqueData = entry.value("uniqueData").toInt(0);
    m_activations.insert(it.key(), ae);
  }

  qInfo() << "ActivationStore: Loaded" << m_categoryVisibility.size()
          << "categories," << m_activations.size() << "activations for"
          << profileId;
}

void ActivationStore::saveNow() {
  if (m_currentProfileId.isEmpty()) {
    return;
  }

  m_saveTimer->stop();

  QJsonObject root;
  root["type"] = "marker_state";
  root["version"] = 1;

  // Category visibility
  QJsonObject catVis;
  for (auto it = m_categoryVisibility.constBegin();
       it != m_categoryVisibility.constEnd(); ++it) {
    catVis[it.key()] = it.value();
  }
  root["categoryVisibility"] = catVis;

  // Activations
  QJsonObject activations;
  for (auto it = m_activations.constBegin(); it != m_activations.constEnd();
       ++it) {
    QJsonObject entry;
    entry["lastActivated"] = it.value().lastActivated.toString(Qt::ISODate);
    entry["uniqueData"] = it.value().uniqueData;
    activations[it.key()] = entry;
  }
  root["activations"] = activations;

  bool ok = AtomicFileWriter::writeJson(m_currentFilePath, root);
  if (ok) {
    qInfo() << "ActivationStore: Saved state for" << m_currentProfileId;
  } else {
    qWarning() << "ActivationStore: Failed to save state for"
               << m_currentProfileId;
  }
}

// --- Category visibility ---

bool ActivationStore::isCategoryVisible(const QString &categoryPath) const {
  return m_categoryVisibility.value(categoryPath, true);
}

void ActivationStore::setCategoryVisible(const QString &categoryPath,
                                         bool visible) {
  m_categoryVisibility[categoryPath] = visible;
  scheduleSave();
  emit categoryVisibilityChanged(categoryPath, visible);
  emit stateChanged();
}

QHash<QString, bool> ActivationStore::allCategoryVisibility() const {
  return m_categoryVisibility;
}

// --- Marker activation ---

QString ActivationStore::activationKey(const QUuid &guid,
                                       int uniqueData) const {
  // TacO pattern: GUID + uniqueData forms the activation key
  return guid.toString(QUuid::WithoutBraces) + "+" +
         QString::number(uniqueData);
}

bool ActivationStore::isActivated(const QUuid &guid, int uniqueData) const {
  return m_activations.contains(activationKey(guid, uniqueData));
}

void ActivationStore::activate(const QUuid &guid, int uniqueData) {
  QString key = activationKey(guid, uniqueData);
  ActivationEntry entry;
  entry.lastActivated = QDateTime::currentDateTimeUtc();
  entry.uniqueData = uniqueData;
  m_activations.insert(key, entry);

  scheduleSave();
  emit markerActivated(guid);
  emit stateChanged();
}

void ActivationStore::deactivate(const QUuid &guid, int uniqueData) {
  QString key = activationKey(guid, uniqueData);
  m_activations.remove(key);

  scheduleSave();
  emit stateChanged();
}

QDateTime ActivationStore::activationTime(const QUuid &guid,
                                          int uniqueData) const {
  QString key = activationKey(guid, uniqueData);
  auto it = m_activations.find(key);
  if (it != m_activations.end()) {
    return it.value().lastActivated;
  }
  return QDateTime();
}

// --- Cleanup ---

void ActivationStore::deleteProfileState(const QString &stateDir,
                                         const QString &profileId) {
  QDir dir(stateDir);
  QStringList suffixes = {".json", ".json.bak", ".json.tmp"};

  for (const QString &suffix : suffixes) {
    QString path = dir.filePath(profileId + suffix);
    if (QFile::exists(path)) {
      QFile::remove(path);
      qInfo() << "ActivationStore: Deleted" << path;
    }
  }
}

// --- Private ---

void ActivationStore::scheduleSave() {
  // Restart the debounce timer — save will fire 500ms after last change
  m_saveTimer->start();
}

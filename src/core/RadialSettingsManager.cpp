/**
 * @file RadialSettingsManager.cpp
 * @brief Per-profile radial settings file I/O implementation
 *
 * DO NOT ADD:
 * - UI code (belongs in RadialTabWidget)
 * - Hardcoded paths (use m_basePath from StorageBackend)
 */

#include "RadialSettingsManager.h"

#include "AtomicFileWriter.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

RadialSettingsManager::RadialSettingsManager(const QString &basePath,
                                             QObject *parent)
    : QObject(parent), m_basePath(basePath), m_settings(RadialSettings::defaults()) {
}

QString RadialSettingsManager::filePath(const QString &profileId) const {
  return QDir(m_basePath).filePath(profileId + ".json");
}

bool RadialSettingsManager::loadForProfile(const QString &profileId) {
  m_currentProfileId = profileId;

  const QString path = filePath(profileId);
  QFile file(path);
  if (!file.exists()) {
    qInfo() << "RadialSettingsManager: No settings file for" << profileId
            << "— using defaults";
    m_settings = RadialSettings::defaults();
    return false;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "RadialSettingsManager: Failed to open" << path;
    m_settings = RadialSettings::defaults();
    return false;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "RadialSettingsManager: JSON parse error in" << path
               << ":" << parseError.errorString();
    m_settings = RadialSettings::defaults();
    return false;
  }

  QJsonObject root = doc.object();

  // Validate type field (versioned file format rule)
  if (root["type"].toString() != "radial_settings") {
    qWarning() << "RadialSettingsManager: Invalid type field in" << path
               << "— expected 'radial_settings', got"
               << root["type"].toString();
    m_settings = RadialSettings::defaults();
    return false;
  }

  m_settings = RadialSettings::fromJson(root);
  qInfo() << "RadialSettingsManager: Loaded settings for" << profileId
          << "version:" << m_settings.schemaVersion;
  return true;
}

bool RadialSettingsManager::saveForProfile(const QString &profileId) {
  m_currentProfileId = profileId;

  const QString path = filePath(profileId);
  QJsonObject json = m_settings.toJson();

  if (!AtomicFileWriter::writeJson(path, json)) {
    qWarning() << "RadialSettingsManager: Failed to write" << path;
    emit settingsChanged(); // Still notify — caller may want to show error
    return false;
  }

  qInfo() << "RadialSettingsManager: Saved settings for" << profileId;
  return true;
}

void RadialSettingsManager::setSettings(const RadialSettings &settings) {
  m_settings = settings;
  emit settingsChanged();
}

void RadialSettingsManager::resetToDefaults() {
  m_settings = RadialSettings::defaults();
  emit settingsChanged();
}

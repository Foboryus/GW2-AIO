/**
 * @file SettingsManager.cpp
 * @brief Manages application settings with import/export
 *
 * DO NOT ADD:
 * - UI code (belongs in SettingsPage or SettingsWidget)
 * - Profile-specific settings (belongs in ProfileManager)
 */

#include "SettingsManager.h"

#include <QDebug>

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent),
      m_settings(new QSettings(QSettings::IniFormat, QSettings::UserScope,
                               "GW2AIO", "GW2AIO", this)) {
  qInfo() << "Settings file:" << m_settings->fileName();
}

SettingsManager::SettingsManager(const QString &filePath, QObject *parent)
    : QObject(parent),
      m_settings(new QSettings(filePath, QSettings::IniFormat, this)) {
  qInfo() << "Settings file:" << m_settings->fileName();
}

QVariant SettingsManager::value(const QString &key,
                                const QVariant &defaultValue) const {
  return m_settings->value(key, defaultValue);
}

void SettingsManager::setValue(const QString &key, const QVariant &value) {
  m_settings->setValue(key, value);
  emit settingsChanged(key);
}

bool SettingsManager::contains(const QString &key) const {
  return m_settings->contains(key);
}

void SettingsManager::remove(const QString &key) {
  m_settings->remove(key);
  emit settingsChanged(key);
}

void SettingsManager::beginGroup(const QString &group) {
  m_settings->beginGroup(group);
}

void SettingsManager::endGroup() { m_settings->endGroup(); }

bool SettingsManager::exportToFile(const QString &filePath) const {
  QJsonObject root;

  // Export all settings
  root["radial"] = exportRadialSettings();
  root["dps"] = exportDPSSettings();
  root["markers"] = exportMarkerSettings();
  root["modules"] = exportModuleSettings();

  // Add metadata
  QJsonObject meta;
  meta["version"] = QCoreApplication::applicationVersion();
  meta["exportDate"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  root["_meta"] = meta;

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "Failed to open file for export:" << filePath;
    return false;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.close();

  emit const_cast<SettingsManager *>(this)->settingsExported(filePath);
  qInfo() << "Settings exported to:" << filePath;
  return true;
}

bool SettingsManager::importFromFile(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open file for import:" << filePath;
    return false;
  }

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();

  if (!doc.isObject()) {
    qWarning() << "Invalid settings file format";
    return false;
  }

  QJsonObject root = doc.object();

  // Import sections
  if (root.contains("radial")) {
    importRadialSettings(root["radial"].toObject());
  }
  if (root.contains("dps")) {
    importDPSSettings(root["dps"].toObject());
  }
  if (root.contains("markers")) {
    importMarkerSettings(root["markers"].toObject());
  }
  if (root.contains("modules")) {
    importModuleSettings(root["modules"].toObject());
  }

  sync();
  emit settingsImported();
  qInfo() << "Settings imported from:" << filePath;
  return true;
}

QJsonObject SettingsManager::exportRadialSettings() const {
  QJsonObject obj;

  m_settings->beginGroup("radial");
  for (const QString &key : m_settings->childKeys()) {
    obj[key] = QJsonValue::fromVariant(m_settings->value(key));
  }
  m_settings->endGroup();

  return obj;
}

QJsonObject SettingsManager::exportDPSSettings() const {
  QJsonObject obj;

  m_settings->beginGroup("dps");
  for (const QString &key : m_settings->childKeys()) {
    obj[key] = QJsonValue::fromVariant(m_settings->value(key));
  }
  m_settings->endGroup();

  return obj;
}

QJsonObject SettingsManager::exportMarkerSettings() const {
  QJsonObject obj;

  m_settings->beginGroup("markers");
  for (const QString &key : m_settings->childKeys()) {
    obj[key] = QJsonValue::fromVariant(m_settings->value(key));
  }
  m_settings->endGroup();

  return obj;
}

QJsonObject SettingsManager::exportModuleSettings() const {
  QJsonObject obj;

  m_settings->beginGroup("modules");
  for (const QString &key : m_settings->childKeys()) {
    obj[key] = QJsonValue::fromVariant(m_settings->value(key));
  }
  m_settings->endGroup();

  return obj;
}

void SettingsManager::importRadialSettings(const QJsonObject &settings) {
  m_settings->beginGroup("radial");
  for (auto it = settings.begin(); it != settings.end(); ++it) {
    m_settings->setValue(it.key(), it.value().toVariant());
  }
  m_settings->endGroup();
}

void SettingsManager::importDPSSettings(const QJsonObject &settings) {
  m_settings->beginGroup("dps");
  for (auto it = settings.begin(); it != settings.end(); ++it) {
    m_settings->setValue(it.key(), it.value().toVariant());
  }
  m_settings->endGroup();
}

void SettingsManager::importMarkerSettings(const QJsonObject &settings) {
  m_settings->beginGroup("markers");
  for (auto it = settings.begin(); it != settings.end(); ++it) {
    m_settings->setValue(it.key(), it.value().toVariant());
  }
  m_settings->endGroup();
}

void SettingsManager::importModuleSettings(const QJsonObject &settings) {
  m_settings->beginGroup("modules");
  for (auto it = settings.begin(); it != settings.end(); ++it) {
    m_settings->setValue(it.key(), it.value().toVariant());
  }
  m_settings->endGroup();
}

QString SettingsManager::settingsFilePath() const {
  return m_settings->fileName();
}

void SettingsManager::sync() { m_settings->sync(); }

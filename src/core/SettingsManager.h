#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>


/**
 * @brief Manages application settings with import/export
 *
 * DO NOT ADD:
 * - Inline implementations (use SettingsManager.cpp)
 */
class SettingsManager : public QObject {
  Q_OBJECT

public:
  explicit SettingsManager(QObject *parent = nullptr);
  explicit SettingsManager(const QString &filePath, QObject *parent = nullptr);

  // Generic settings access
  QVariant value(const QString &key,
                 const QVariant &defaultValue = QVariant()) const;
  void setValue(const QString &key, const QVariant &value);
  bool contains(const QString &key) const;
  void remove(const QString &key);

  // Grouped settings
  void beginGroup(const QString &group);
  void endGroup();

  // Import/Export
  bool exportToFile(const QString &filePath) const;
  bool importFromFile(const QString &filePath);

  // Export specific sections
  QJsonObject exportRadialSettings() const;
  QJsonObject exportDPSSettings() const;
  QJsonObject exportMarkerSettings() const;
  QJsonObject exportModuleSettings() const;

  // Import specific sections
  void importRadialSettings(const QJsonObject &settings);
  void importDPSSettings(const QJsonObject &settings);
  void importMarkerSettings(const QJsonObject &settings);
  void importModuleSettings(const QJsonObject &settings);

  // Get settings file path
  QString settingsFilePath() const;

  // Sync to disk
  void sync();

signals:
  void settingsChanged(const QString &key);
  void settingsImported();
  void settingsExported(const QString &filePath);

private:
  QSettings *m_settings;
};

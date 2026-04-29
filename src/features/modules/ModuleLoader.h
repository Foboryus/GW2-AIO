#pragma once

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QList>
#include <QObject>
#include <QStandardPaths>
#include <QString>


#include "BlishModels.h"
#include "NetHost.h"

/**
 * @brief Loads and manages Blish-HUD modules (.bhm files)
 *
 * DO NOT ADD:
 * - Inline implementations (use ModuleLoader.cpp)
 */
class ModuleLoader : public QObject {
  Q_OBJECT

public:
  explicit ModuleLoader(QObject *parent = nullptr);
  ~ModuleLoader();

  /**
   * @brief Initialize the module loader and .NET runtime
   */
  bool initialize();

  /**
   * @brief Get all discovered modules
   */
  const QList<LoadedModule> &modules() const { return m_modules; }

  /**
   * @brief Scan directory for .bhm files
   */
  void scanDirectory(const QString &path);

  /**
   * @brief Load a specific module
   */
  bool loadModule(const QString &moduleId);

  /**
   * @brief Unload a module
   */
  bool unloadModule(const QString &moduleId);

  /**
   * @brief Enable/disable a module
   */
  void setModuleEnabled(const QString &moduleId, bool enabled);

  /**
   * @brief Get default modules directory
   */
  static QString defaultModulesPath();

  /**
   * @brief Check if .NET runtime is available
   */
  bool isRuntimeAvailable() const;

signals:
  void moduleDiscovered(const LoadedModule &module);
  void moduleLoaded(const QString &moduleId);
  void moduleUnloaded(const QString &moduleId);
  void moduleError(const QString &moduleId, const QString &error);

private:
  bool extractBhm(const QString &bhmPath, const QString &extractPath);
  BlishModuleManifest parseManifest(const QString &manifestPath);
  LoadedModule *findModule(const QString &moduleId);

  NetHost *m_netHost;
  QList<LoadedModule> m_modules;
  QString m_modulesPath;
  QString m_extractPath;
};

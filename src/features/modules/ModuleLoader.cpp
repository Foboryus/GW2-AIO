/**
 * @file ModuleLoader.cpp
 * @brief Loads and manages Blish-HUD modules (.bhm files)
 *
 * DO NOT ADD:
 * - Module rendering (belongs in ModuleController)
 * - Graphics bridge (belongs in BlishGraphicsBridge)
 */

#include "ModuleLoader.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

ModuleLoader::ModuleLoader(QObject *parent)
    : QObject(parent), m_netHost(new NetHost()) {
  m_modulesPath = defaultModulesPath();
  m_extractPath =
      QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
          .filePath("GW2AIO_Modules");
}

ModuleLoader::~ModuleLoader() {
  // Unload all modules
  for (auto &module : m_modules) {
    if (module.isRunning()) {
      unloadModule(module.id());
    }
  }

  delete m_netHost;
}

bool ModuleLoader::initialize() {
  // Create directories
  QDir().mkpath(m_modulesPath);
  QDir().mkpath(m_extractPath);

  // Check for .NET runtime
  if (!NetHost::isRuntimeAvailable()) {
    qWarning() << ".NET runtime not found - Blish modules will be disabled";
    qWarning() << "Install .NET 6+ from https://dotnet.microsoft.com/download";
    return false;
  }

  qInfo() << ".NET runtime version:" << NetHost::runtimeVersion();

  // Initialize runtime
  if (!m_netHost->initialize()) {
    qWarning() << "Failed to initialize .NET host:" << m_netHost->lastError();
    return false;
  }

  // Scan for modules
  scanDirectory(m_modulesPath);

  return true;
}

void ModuleLoader::scanDirectory(const QString &path) {
  QDir dir(path);

  if (!dir.exists()) {
    qWarning() << "Modules directory does not exist:" << path;
    return;
  }

  // Find .bhm files
  QStringList bhmFiles = dir.entryList({"*.bhm"}, QDir::Files);

  for (const QString &bhmFile : bhmFiles) {
    QString bhmPath = dir.filePath(bhmFile);
    QString extractDir =
        QDir(m_extractPath).filePath(QFileInfo(bhmFile).baseName());

    // Extract if needed
    if (!QDir(extractDir).exists()) {
      if (!extractBhm(bhmPath, extractDir)) {
        qWarning() << "Failed to extract module:" << bhmFile;
        continue;
      }
    }

    // Parse manifest
    QString manifestPath = QDir(extractDir).filePath("manifest.json");
    if (!QFile::exists(manifestPath)) {
      qWarning() << "No manifest.json in module:" << bhmFile;
      continue;
    }

    BlishModuleManifest manifest = parseManifest(manifestPath);
    if (manifest.name.isEmpty()) {
      continue;
    }

    // Create module entry
    LoadedModule module;
    module.manifest = manifest;
    module.bhmPath = bhmPath;
    module.extractPath = extractDir;
    module.state = LoadedModule::State::Unloaded;

    // Find entry DLL
    QDir extractedDir(extractDir);
    QStringList dlls = extractedDir.entryList({"*.dll"}, QDir::Files);
    for (const QString &dll : dlls) {
      if (dll.contains(manifest.moduleNamespace, Qt::CaseInsensitive) ||
          dll.contains(manifest.name, Qt::CaseInsensitive)) {
        module.dllPath = extractedDir.filePath(dll);
        break;
      }
    }

    m_modules.append(module);
    emit moduleDiscovered(module);

    qInfo() << "Discovered module:" << manifest.name << "v" << manifest.version;
  }

  qInfo() << "Found" << m_modules.size() << "Blish-HUD modules";
}

bool ModuleLoader::loadModule(const QString &moduleId) {
  LoadedModule *module = findModule(moduleId);
  if (!module) {
    emit moduleError(moduleId, "Module not found");
    return false;
  }

  if (module->dllPath.isEmpty()) {
    module->state = LoadedModule::State::Error;
    module->errorMessage = "No entry DLL found";
    emit moduleError(moduleId, module->errorMessage);
    return false;
  }

  module->state = LoadedModule::State::Loading;

  // Load assembly via .NET host
  void *handle = m_netHost->loadAssembly(module->dllPath);
  if (!handle) {
    module->state = LoadedModule::State::Error;
    module->errorMessage = m_netHost->lastError();
    emit moduleError(moduleId, module->errorMessage);
    return false;
  }

  module->moduleHandle = handle;
  module->state = LoadedModule::State::Loaded;

  emit moduleLoaded(moduleId);
  qInfo() << "Loaded module:" << module->manifest.name;

  return true;
}

bool ModuleLoader::unloadModule(const QString &moduleId) {
  LoadedModule *module = findModule(moduleId);
  if (!module)
    return false;

  module->moduleHandle = nullptr;
  module->updateDelegate = nullptr;
  module->renderDelegate = nullptr;
  module->state = LoadedModule::State::Unloaded;

  emit moduleUnloaded(moduleId);
  return true;
}

void ModuleLoader::setModuleEnabled(const QString &moduleId, bool enabled) {
  LoadedModule *module = findModule(moduleId);
  if (!module)
    return;

  module->enabled = enabled;
  module->state =
      enabled ? LoadedModule::State::Unloaded : LoadedModule::State::Disabled;
}

QString ModuleLoader::defaultModulesPath() {
  QString appData =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return QDir(appData).filePath("BlishModules");
}

bool ModuleLoader::isRuntimeAvailable() const {
  return NetHost::isRuntimeAvailable();
}

bool ModuleLoader::extractBhm(const QString &bhmPath,
                              const QString &extractPath) {
  // .bhm files are ZIP archives
  // TODO: Use QuaZip for extraction
  // For now, assume pre-extracted or manual extraction

  Q_UNUSED(bhmPath);
  QDir().mkpath(extractPath);

  qInfo() << "Note: .bhm extraction requires QuaZip - extract manually to:"
          << extractPath;
  return QDir(extractPath).exists();
}

BlishModuleManifest ModuleLoader::parseManifest(const QString &manifestPath) {
  BlishModuleManifest manifest;

  QFile file(manifestPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return manifest;
  }

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();

  if (doc.isObject()) {
    manifest = BlishModuleManifest::fromJson(doc.object());
  }

  return manifest;
}

LoadedModule *ModuleLoader::findModule(const QString &moduleId) {
  for (auto &module : m_modules) {
    if (module.id() == moduleId) {
      return &module;
    }
  }
  return nullptr;
}

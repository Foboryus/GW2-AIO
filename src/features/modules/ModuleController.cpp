/**
 * @file ModuleController.cpp
 * @brief Main controller for Blish-HUD module system
 *
 * DO NOT ADD:
 * - .NET runtime initialization (belongs in ModuleLoader)
 * - Module discovery (belongs in ModuleLoader)
 */

#include "ModuleController.h"

#include <QDebug>

ModuleController::ModuleController(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumble(mumble), m_loader(new ModuleLoader(this)),
      m_updateTimer(new QTimer(this)) {
  connect(m_loader, &ModuleLoader::moduleLoaded, this,
          &ModuleController::onModuleLoaded);
  connect(m_loader, &ModuleLoader::moduleError, this,
          &ModuleController::onModuleError);
  connect(m_updateTimer, &QTimer::timeout, this,
          &ModuleController::updateModules);
}

ModuleController::~ModuleController() { stop(); }

bool ModuleController::start() {
  if (!m_loader->isRuntimeAvailable()) {
    qWarning() << "Blish-HUD modules disabled - .NET runtime not found";
    emit runtimeNotAvailable();
    return false;
  }

  if (!m_loader->initialize()) {
    qWarning() << "Failed to initialize module loader";
    return false;
  }

  // Start update loop (60fps for modules)
  m_updateTimer->start(16);

  qInfo() << "Module system started with" << modules().size() << "modules";
  return true;
}

void ModuleController::stop() {
  m_updateTimer->stop();

  // Unload all running modules
  for (const auto &module : modules()) {
    if (module.isRunning()) {
      unloadModule(module.id());
    }
  }
}

void ModuleController::setMumbleLink(MumbleLink *mumble) {
  m_mumble = mumble;
}

const QList<LoadedModule> &ModuleController::modules() const {
  return m_loader->modules();
}

bool ModuleController::loadModule(const QString &moduleId) {
  return m_loader->loadModule(moduleId);
}

bool ModuleController::unloadModule(const QString &moduleId) {
  return m_loader->unloadModule(moduleId);
}

bool ModuleController::isRuntimeAvailable() const {
  return m_loader->isRuntimeAvailable();
}

void ModuleController::updateModules() {
  // Update game state from Mumble
  updateGameState();

  // Call update on each running module
  // TODO: When full .NET interop is implemented, call module Update methods
}

void ModuleController::updateGameState() {
  if (!m_mumble) {
    m_gameState.isInGame = false;
    return;
  }
  m_gameState.isInGame = m_mumble->isConnected();
  m_gameState.mapId = m_mumble->mapId();
  m_gameState.playerX = m_mumble->playerX();
  m_gameState.playerY = m_mumble->playerY();
  m_gameState.playerZ = m_mumble->playerZ();
  m_gameState.characterName = m_mumble->characterName();
}

void ModuleController::onModuleLoaded(const QString &moduleId) {
  emit moduleStatusChanged(moduleId, LoadedModule::State::Loaded);
}

void ModuleController::onModuleError(const QString &moduleId,
                                     const QString &error) {
  qWarning() << "Module error:" << moduleId << "-" << error;
  emit moduleStatusChanged(moduleId, LoadedModule::State::Error);
}

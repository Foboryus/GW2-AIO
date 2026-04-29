#pragma once

#include <QObject>
#include <QTimer>

#include "BlishModels.h"
#include "ModuleLoader.h"
#include "core/MumbleLink.h"

// Forward declare or use simple struct for game state
namespace BlishAPI {
struct GameState {
  bool isInGame = false;
  int mapId = 0;
  float playerX = 0, playerY = 0, playerZ = 0;
  QString characterName;
};
} // namespace BlishAPI

/**
 * @brief Main controller for Blish-HUD module system
 *
 * DO NOT ADD:
 * - Inline implementations (use ModuleController.cpp)
 */
class ModuleController : public QObject {
  Q_OBJECT

public:
  explicit ModuleController(MumbleLink *mumble, QObject *parent = nullptr);
  ~ModuleController();

  /**
   * @brief Initialize the module system
   */
  bool start();

  /**
   * @brief Stop and cleanup
   */
  void stop();

  /**
   * @brief Update the MumbleLink reference dynamically (Phase 7b-2b)
   */
  void setMumbleLink(MumbleLink *mumble);

  /**
   * @brief Get the module loader
   */
  ModuleLoader *loader() { return m_loader; }

  /**
   * @brief Get all modules
   */
  const QList<LoadedModule> &modules() const;

  /**
   * @brief Load a module by ID
   */
  bool loadModule(const QString &moduleId);

  /**
   * @brief Unload a module
   */
  bool unloadModule(const QString &moduleId);

  /**
   * @brief Check if .NET runtime is available
   */
  bool isRuntimeAvailable() const;

signals:
  void moduleStatusChanged(const QString &moduleId, LoadedModule::State state);
  void runtimeNotAvailable();

private slots:
  void updateModules();
  void onModuleLoaded(const QString &moduleId);
  void onModuleError(const QString &moduleId, const QString &error);

private:
  void updateGameState();

  MumbleLink *m_mumble;
  ModuleLoader *m_loader;
  QTimer *m_updateTimer;

  BlishAPI::GameState m_gameState;
};

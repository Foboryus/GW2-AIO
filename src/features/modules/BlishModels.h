#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>


/**
 * @brief Blish-HUD module manifest structure
 *
 * .bhm files are ZIP archives containing:
 * - manifest.json
 * - [ModuleName].dll
 * - Other resources
 */
struct BlishModuleManifest {
  // Required fields
  QString name;
  QString version;
  QString moduleNamespace; // C# namespace (was @namespace - invalid in C++)
  QString package;         // Package identifier

  // Optional fields
  QString description;
  QString author;
  QString url;
  QStringList dependencies;

  // API compatibility
  int apiVersion = 1;
  QString blishHudVersion; // Minimum Blish version

  // Entry point
  QString entryDll;   // Main DLL file
  QString entryClass; // Module class name

  static BlishModuleManifest fromJson(const QJsonObject &json) {
    BlishModuleManifest m;

    m.name = json["name"].toString();
    m.version = json["version"].toString();
    m.moduleNamespace = json["namespace"].toString();
    m.package = json["package"].toString();
    m.description = json["description"].toString();
    m.author = json["author"].toString();
    m.url = json["url"].toString();
    m.apiVersion = json["api_version"].toInt(1);
    m.blishHudVersion = json["blish_hud_version"].toString();

    // Parse dependencies
    for (const auto &dep : json["dependencies"].toArray()) {
      m.dependencies.append(dep.toString());
    }

    return m;
  }

  QJsonObject toJson() const {
    QJsonObject json;
    json["name"] = name;
    json["version"] = version;
    json["namespace"] = moduleNamespace;
    json["package"] = package;
    json["description"] = description;
    json["author"] = author;
    json["url"] = url;
    json["api_version"] = apiVersion;
    return json;
  }
};

/**
 * @brief Runtime state of a loaded module
 */
struct LoadedModule {
  enum class State { Unloaded, Loading, Loaded, Running, Error, Disabled };

  BlishModuleManifest manifest;
  QString bhmPath;     // Path to .bhm file
  QString extractPath; // Extracted directory
  QString dllPath;     // Path to entry DLL

  State state = State::Unloaded;
  QString errorMessage;

  // Runtime handles
  void *moduleHandle = nullptr;
  void *updateDelegate = nullptr;
  void *renderDelegate = nullptr;

  // Settings
  bool enabled = true;
  QJsonObject settings;

  QString id() const { return manifest.package; }
  bool isRunning() const { return state == State::Running; }
};

/**
 * @brief Blish-HUD API compatibility layer
 *
 * Provides mock implementations of Blish-HUD APIs that modules depend on
 * Note: GraphicsService, SettingsService, etc. are in BlishAPIServices.h
 */
namespace BlishAPI {
// Game service data (basic version - full version in BlishAPIServices.h)
struct GameServiceBasic {
  bool isInGame = false;
  uint32_t mapId = 0;
  float playerX = 0, playerY = 0, playerZ = 0;
  QString characterName;
  int profession = 0;
  bool isCommander = false;
  bool isInCombat = false;
};
} // namespace BlishAPI

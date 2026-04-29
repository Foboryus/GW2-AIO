#pragma once

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPoint>
#include <QStandardPaths>
#include <QString>


#include "BlishGraphicsBridge.h"
#include "core/MumbleLink.h"


/**
 * @brief Full Blish-HUD API implementation
 *
 * This provides all the services that Blish-HUD modules expect:
 * - GameService (Mumble Link data)
 * - Gw2ApiManager (GW2 API access)
 * - GraphicsService (screen info)
 * - InputService (keyboard/mouse)
 * - OverlayService (window management)
 * - DirectoriesService (file paths)
 *
 * DO NOT ADD:
 * - Inline implementations (use BlishAPIServices.cpp)
 * - Graphics rendering logic (belongs in BlishGraphicsBridge)
 */
namespace BlishAPI {

/**
 * @brief Game state service (from Mumble Link)
 */
class GameService : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool isInGame READ isInGame NOTIFY isInGameChanged)
  Q_PROPERTY(int mapId READ mapId NOTIFY mapIdChanged)
  Q_PROPERTY(
      QString characterName READ characterName NOTIFY characterNameChanged)

public:
  explicit GameService(MumbleLink *mumble, QObject *parent = nullptr);
  
  void setMumbleLink(MumbleLink *mumble);

  bool isInGame() const { return m_mumble->isConnected(); }
  int mapId() const { return m_mumble->mapId(); }
  QString characterName() const { return m_mumble->characterName(); }

  // Position
  float playerX() const { return m_mumble->playerX(); }
  float playerY() const { return m_mumble->playerY(); }
  float playerZ() const { return m_mumble->playerZ(); }

  // Identity
  QString identity() const { return m_mumble->identity(); }
  bool isCommander() const { return m_isCommander; }
  int profession() const { return m_profession; }
  int specialization() const { return m_specialization; }
  int race() const { return m_race; }

  // UI state
  bool isMapOpen() const { return m_mumble->uiState() & 0x01; }
  bool isCompassTopRight() const { return m_mumble->uiState() & 0x02; }
  bool isCompassRotating() const { return m_mumble->uiState() & 0x04; }
  bool isInCombat() const { return m_mumble->uiState() & 0x08; }
  bool isTextInputFocused() const { return m_mumble->uiState() & 0x10; }
  bool isCompetitiveMode() const { return m_mumble->uiState() & 0x20; }

signals:
  void isInGameChanged(bool inGame);
  void mapIdChanged(int mapId);
  void characterNameChanged(const QString &name);
  void identityChanged(const QString &identity);

private slots:
  void updateFromMumble();

private:
  MumbleLink *m_mumble;
  bool m_isCommander = false;
  int m_profession = 0;
  int m_specialization = 0;
  int m_race = 0;
};

/**
 * @brief GW2 API access service
 */
class Gw2ApiManager : public QObject {
  Q_OBJECT

public:
  explicit Gw2ApiManager(QObject *parent = nullptr);

  void setApiKey(const QString &key) { m_apiKey = key; }
  void request(const QString &endpoint);
  void requestAccount();
  void requestCharacter(const QString &name);

signals:
  void responseReceived(const QString &endpoint, const QJsonObject &data);
  void errorOccurred(const QString &endpoint, const QString &error);

private:
  QNetworkAccessManager *m_network;
  QString m_apiKey;
  QString m_baseUrl = "https://api.guildwars2.com/v2";
};

/**
 * @brief Graphics/screen service
 */
class GraphicsService : public QObject {
  Q_OBJECT

public:
  explicit GraphicsService(QObject *parent = nullptr);

  int screenWidth() const { return m_width; }
  int screenHeight() const { return m_height; }
  float uiScale() const { return m_uiScale; }

  void updateScreenSize(int width, int height);
  void setUiScale(float scale) { m_uiScale = scale; }

  BlishGraphicsBridge *graphicsBridge() { return m_bridge; }

signals:
  void screenSizeChanged(int width, int height);

private:
  int m_width = 1920;
  int m_height = 1080;
  float m_uiScale = 1.0f;
  BlishGraphicsBridge *m_bridge;
};

/**
 * @brief Input service for keyboard/mouse
 */
class InputService : public QObject {
  Q_OBJECT

public:
  explicit InputService(QObject *parent = nullptr);

  bool isKeyDown(int keyCode) const;
  bool isMouseButtonDown(int button) const;
  QPoint mousePosition() const { return m_mousePos; }

  void setKeyState(int keyCode, bool down);
  void setMousePos(const QPoint &pos) { m_mousePos = pos; }
  void setMouseButton(int button, bool down);

signals:
  void keyPressed(int keyCode);
  void keyReleased(int keyCode);
  void mousePressed(int button, const QPoint &pos);
  void mouseReleased(int button, const QPoint &pos);
  void mouseMoved(const QPoint &pos);

private:
  QMap<int, bool> m_keyStates;
  QMap<int, bool> m_mouseButtons;
  QPoint m_mousePos;
};

/**
 * @brief Overlay window service
 */
class OverlayService : public QObject {
  Q_OBJECT

public:
  explicit OverlayService(QObject *parent = nullptr);

  bool isVisible() const { return m_visible; }
  void setVisible(bool visible);

  void showTooltip(const QString &text, const QPoint &position);
  void hideTooltip();

signals:
  void visibilityChanged(bool visible);
  void tooltipRequested(const QString &text, const QPoint &position);

private:
  bool m_visible = true;
};

/**
 * @brief File/directory path service
 */
class DirectoriesService : public QObject {
  Q_OBJECT

public:
  explicit DirectoriesService(QObject *parent = nullptr);

  QString basePath() const { return m_basePath; }
  QString settingsPath() const;
  QString modulesPath() const;
  QString cachePath() const;

  void setBasePath(const QString &path) { m_basePath = path; }

private:
  QString m_basePath;
};

/**
 * @brief Master service container for modules
 */
class ServiceManager : public QObject {
  Q_OBJECT

public:
  explicit ServiceManager(MumbleLink *mumble, QObject *parent = nullptr);
  
  void setMumbleLink(MumbleLink *mumble);

  GameService *gameService() { return m_game; }
  Gw2ApiManager *apiManager() { return m_api; }
  GraphicsService *graphicsService() { return m_graphics; }
  InputService *inputService() { return m_input; }
  OverlayService *overlayService() { return m_overlay; }
  DirectoriesService *directoriesService() { return m_directories; }

private:
  GameService *m_game;
  Gw2ApiManager *m_api;
  GraphicsService *m_graphics;
  InputService *m_input;
  OverlayService *m_overlay;
  DirectoriesService *m_directories;
};

} // namespace BlishAPI

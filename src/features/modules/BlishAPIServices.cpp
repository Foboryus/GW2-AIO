/**
 * @file BlishAPIServices.cpp
 * @brief Full Blish-HUD API implementation
 *
 * This file contains the implementations for all Blish API service classes:
 * GameService, Gw2ApiManager, GraphicsService, InputService, OverlayService,
 * DirectoriesService, and ServiceManager.
 *
 * DO NOT ADD:
 * - Graphics rendering logic (belongs in BlishGraphicsBridge)
 * - Module loading logic (belongs in ModuleLoader)
 * - Non-API related functionality
 */

#include "BlishAPIServices.h"

namespace BlishAPI {

// ===============================
// GameService
// ===============================

GameService::GameService(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumble(mumble) {
  if (m_mumble) {
    connect(m_mumble, &MumbleLink::mapChanged, this, &GameService::mapIdChanged);
    connect(m_mumble, &MumbleLink::connectedChanged, this,
            [this](bool connected) { emit isInGameChanged(connected); });
  }
}

void GameService::setMumbleLink(MumbleLink *mumble) {
  if (m_mumble == mumble) return;

  if (m_mumble) {
    disconnect(m_mumble, &MumbleLink::mapChanged, this, &GameService::mapIdChanged);
    disconnect(m_mumble, &MumbleLink::connectedChanged, this, nullptr);
  }

  m_mumble = mumble;

  if (m_mumble) {
    connect(m_mumble, &MumbleLink::mapChanged, this, &GameService::mapIdChanged);
    connect(m_mumble, &MumbleLink::connectedChanged, this,
            [this](bool connected) { emit isInGameChanged(connected); });
            
    emit mapIdChanged(m_mumble->mapId());
    emit isInGameChanged(m_mumble->isConnected());
  } else {
    emit isInGameChanged(false);
  }
}

void GameService::updateFromMumble() {
  // Parse identity JSON for additional info
  QString id = m_mumble->identity();
  if (!id.isEmpty()) {
    QJsonDocument doc = QJsonDocument::fromJson(id.toUtf8());
    QJsonObject obj = doc.object();

    m_isCommander = obj["commander"].toBool();
    m_profession = obj["profession"].toInt();
    m_specialization = obj["spec"].toInt();
    m_race = obj["race"].toInt();

    emit identityChanged(id);
  }
}

// ===============================
// Gw2ApiManager
// ===============================

Gw2ApiManager::Gw2ApiManager(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {
  connect(m_network, &QNetworkAccessManager::finished, this,
          [this](QNetworkReply *reply) {
            QString endpoint = reply->property("endpoint").toString();

            if (reply->error() == QNetworkReply::NoError) {
              QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
              emit responseReceived(endpoint, doc.object());
            } else {
              emit errorOccurred(endpoint, reply->errorString());
            }

            reply->deleteLater();
          });
}

void Gw2ApiManager::request(const QString &endpoint) {
  QString url = m_baseUrl + "/" + endpoint;
  if (!m_apiKey.isEmpty()) {
    url += "?access_token=" + m_apiKey;
  }

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "GW2AIO/1.0");

  QNetworkReply *reply = m_network->get(req);
  reply->setProperty("endpoint", endpoint);
}

void Gw2ApiManager::requestAccount() { request("account"); }
void Gw2ApiManager::requestCharacter(const QString &name) {
  request("characters/" + name);
}

// ===============================
// GraphicsService
// ===============================

GraphicsService::GraphicsService(QObject *parent)
    : QObject(parent), m_bridge(new BlishGraphicsBridge(this)) {}

void GraphicsService::updateScreenSize(int width, int height) {
  if (m_width != width || m_height != height) {
    m_width = width;
    m_height = height;
    emit screenSizeChanged(width, height);
  }
}

// ===============================
// InputService
// ===============================

InputService::InputService(QObject *parent) : QObject(parent) {}

bool InputService::isKeyDown(int keyCode) const {
  return m_keyStates.value(keyCode, false);
}

bool InputService::isMouseButtonDown(int button) const {
  return m_mouseButtons.value(button, false);
}

void InputService::setKeyState(int keyCode, bool down) {
  if (m_keyStates.value(keyCode) != down) {
    m_keyStates[keyCode] = down;
    if (down)
      emit keyPressed(keyCode);
    else
      emit keyReleased(keyCode);
  }
}

void InputService::setMouseButton(int button, bool down) {
  if (m_mouseButtons.value(button) != down) {
    m_mouseButtons[button] = down;
    if (down)
      emit mousePressed(button, m_mousePos);
    else
      emit mouseReleased(button, m_mousePos);
  }
}

// ===============================
// OverlayService
// ===============================

OverlayService::OverlayService(QObject *parent) : QObject(parent) {}

void OverlayService::setVisible(bool visible) {
  if (m_visible != visible) {
    m_visible = visible;
    emit visibilityChanged(visible);
  }
}

void OverlayService::showTooltip(const QString &text, const QPoint &position) {
  emit tooltipRequested(text, position);
}

void OverlayService::hideTooltip() {
  emit tooltipRequested(QString(), QPoint());
}

// ===============================
// DirectoriesService
// ===============================

DirectoriesService::DirectoriesService(QObject *parent) : QObject(parent) {
  m_basePath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString DirectoriesService::settingsPath() const {
  return QDir(m_basePath).filePath("ModuleSettings");
}

QString DirectoriesService::modulesPath() const {
  return QDir(m_basePath).filePath("BlishModules");
}

QString DirectoriesService::cachePath() const {
  return QDir(m_basePath).filePath("Cache");
}

// ===============================
// ServiceManager
// ===============================

ServiceManager::ServiceManager(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_game(new GameService(mumble, this)),
      m_api(new Gw2ApiManager(this)), m_graphics(new GraphicsService(this)),
      m_input(new InputService(this)), m_overlay(new OverlayService(this)),
      m_directories(new DirectoriesService(this)) {}

void ServiceManager::setMumbleLink(MumbleLink *mumble) {
  m_game->setMumbleLink(mumble);
}

} // namespace BlishAPI

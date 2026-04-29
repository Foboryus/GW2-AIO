/**
 * @file MarkerController.cpp
 * @brief Main controller for the marker system
 *
 * DO NOT ADD:
 * - Marker rendering (belongs in MarkerRenderer)
 * - Pack parsing (belongs in MarkerManager)
 */

#include "MarkerController.h"
#include "ActivationStore.h"
#include "Gw2ApiClient.h"
#include "ImageCache.h"
#include "MarkerPackRegistry.h"
#include "MarkerSettingsManager.h"

#include <QDebug>

MarkerController::MarkerController(QObject *parent)
    : QObject(parent), m_mumble(nullptr), m_imageCache(new ImageCache(this)),
      m_manager(new MarkerManager(nullptr, this)),
      m_renderer(new MarkerRenderer(m_manager, nullptr)),
      m_minimapRenderer(new MinimapRenderer(m_manager, nullptr, m_imageCache)),
      m_apiClient(new Gw2ApiClient(this)) {
  m_packsPath = defaultPacksPath();
  m_registry = new MarkerPackRegistry(m_packsPath, this);

  // Reload packs after a download completes
  connect(m_registry, &MarkerPackRegistry::downloadFinished, this,
          [this](const QString & /*packId*/, bool success,
                 const QString & /*error*/) {
            if (success) {
              reloadPacks();
            }
          });

  connect(m_manager, &MarkerManager::packsLoaded, this, [this]() {
    int packCount = m_manager->packs().size();
    int markerCount = 0;
    for (const auto &pack : m_manager->packs()) {
      markerCount += pack.markerCount();
    }
    emit packsLoaded(packCount, markerCount);
  });
}

MarkerController::~MarkerController() {
  stop();
  delete m_renderer;
  delete m_minimapRenderer;
}

void MarkerController::start() {
  // Ensure packs directory exists
  QDir dir(m_packsPath);
  if (!dir.exists()) {
    dir.mkpath(".");
    qInfo() << "Created marker packs directory:" << m_packsPath;
  }

  // Load packs asynchronously (background thread)
  m_manager->loadPacksAsync(m_packsPath);

  // Load online pack manifest and scan for installed packs
  m_registry->loadManifest();

  qInfo() << "Marker system started - Packs path:" << m_packsPath;
}

void MarkerController::stop() {
  m_renderer->hide();
  m_minimapRenderer->stop();
}

void MarkerController::setPacksDirectory(const QString &path) {
  m_packsPath = path;
}

void MarkerController::setCacheDir(const QString &dir) {
  m_manager->setCacheDir(dir);
  m_registry->setCacheDir(dir);
}

void MarkerController::reloadPacks() {
  m_manager->loadPacksFromDirectory(m_packsPath);
}

// NOTE: Not called in multibox architecture — per-instance MinimapRenderer
// lifecycle is managed by OverlayInstance::start()/stop() which connects
// directly to per-instance MumbleLink::connectionChanged.
// Kept for potential single-instance fallback path.
void MarkerController::setVisible(bool visible) {
  if (visible) {
    // NOTE: m_renderer (QPainter fallback) is intentionally NOT shown.
    // 3D marker rendering is handled by the D3D11 pipeline.
    // m_renderer->showFullScreen() was placing a transparent window on the
    // primary monitor, causing white lines/circles on the wrong screen.
    m_minimapRenderer->start();
  } else {
    m_renderer->hide();
    m_minimapRenderer->stop();
  }

  // Pause proximity timer when overlay is hidden to save CPU.
  // Daily reset timer always runs (markers should restore in background).
  m_manager->setProximityEnabled(visible);
}

bool MarkerController::isVisible() const { return m_renderer->isVisible(); }

void MarkerController::setActivationStore(ActivationStore *store) {
  m_manager->setActivationStore(store);
}

void MarkerController::setMarkerSettings(MarkerSettingsManager *settings) {
  m_manager->setMarkerSettings(settings);

  // Wire settings changes to minimap renderer
  if (settings) {
    auto syncMinimapSettings = [this, settings]() {
      bool mainOn = settings->renderingEnabled();
      m_minimapRenderer->setOpacity(
          static_cast<float>(settings->minimapOpacity()));
      m_minimapRenderer->setMinimapMarkerScale(
          static_cast<float>(settings->minimapMarkerScale()));
      m_minimapRenderer->setMinimapMarkerOpacity(
          static_cast<float>(settings->minimapMarkerOpacity()));
      m_minimapRenderer->setShowMinimapMarkers(
          mainOn && settings->renderMinimapEnabled());
      m_minimapRenderer->setShowBigMapMarkers(
          mainOn && settings->renderBigMapEnabled());
    };

    QObject::connect(settings, &MarkerSettingsManager::settingsChanged, this,
                     syncMinimapSettings);
    // Apply initial state
    syncMinimapSettings();

    // Reload packs when a pack is re-enabled (lazy loading: disabled packs
    // aren't loaded, so enabling requires a full reload to parse them)
    QObject::connect(
        settings, &MarkerSettingsManager::packEnabledChanged, this,
        [this](const QString &packId, bool enabled) {
          if (enabled) {
            qInfo() << "MarkerController: Pack re-enabled:" << packId
                    << "— reloading packs";
            reloadPacks();
          }
        });
  }
}

void MarkerController::loadProfileState(const QString &profileId) {
  if (auto *store = m_manager->activationStore()) {
    store->loadForProfile(profileId);
    m_manager->restoreActivationState();
  }
}

void MarkerController::setApiKey(const QString &key) {
  m_apiClient->setApiKey(key);
}

QString MarkerController::defaultPacksPath() const {
  QString appData =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return QDir(appData).filePath("MarkerPacks");
}

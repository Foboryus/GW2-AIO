/**
 * @file OverlayInstance.cpp
 * @brief Per-GW2-process overlay bundle implementation
 *
 * See OverlayInstance.h for class documentation.
 */

#include "OverlayInstance.h"

#include "MumbleLink.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerController.h"
#include "features/markers/MarkerSettingsManager.h"
#include "features/markers/MinimapRenderer.h"
#include "rendering/D3D11OverlayWindow.h"
#include "ui/OverlayWindow.h"

#include <QDebug>

// ============================================================================
// Constructor / Destructor
// ============================================================================

OverlayInstance::OverlayInstance(const QString &profileId,
                                 const QString &mumbleLinkName,
                                 const QString &markerStateDir,
                                 MarkerController *markerController,
                                 ImageCache *imageCache,
                                 QObject *parent)
    : QObject(parent), m_profileId(profileId), m_mumbleLinkName(mumbleLinkName),
      m_markerController(markerController) {

  // Create owned components on heap — does NOT start them.
  // Each component receives the per-instance MumbleLink so their constructors
  // wire internal signals (connectionChanged, dataUpdated, etc.) automatically.
  m_mumbleLink = new MumbleLink(m_mumbleLinkName, this);
  m_markerSettings = new MarkerSettingsManager(markerStateDir, this);

  // Per-instance MinimapRenderer: uses per-instance MumbleLink for compass
  // data and shared MarkerManager for marker visibility data.
  // Not parented to OverlayWindow yet — reparented in start().
  m_minimapRenderer = new MinimapRenderer(
      m_markerController->manager(), m_mumbleLink, imageCache);

  // OverlayWindow is a QWidget — no QObject parent (top-level window).
  // OverlayInstance destructor handles cleanup explicitly.
  m_overlayWindow = new OverlayWindow(m_mumbleLink);

  m_d3dOverlay = new D3D11OverlayWindow(m_mumbleLink, this);

  qInfo() << "OverlayInstance: created for profile:" << m_profileId
          << "mumbleLink:" << m_mumbleLinkName;
}

OverlayInstance::~OverlayInstance() {
  if (m_running) {
    stop();
  }

  // OverlayWindow is a top-level QWidget (no QObject parent),
  // so we must delete it explicitly.
  delete m_overlayWindow;
  m_overlayWindow = nullptr;

  // MinimapRenderer was reparented to OverlayWindow in start() —
  // it was deleted when OverlayWindow was deleted above.
  // Set to nullptr to avoid double-delete.
  m_minimapRenderer = nullptr;

  // m_mumbleLink, m_markerSettings, m_d3dOverlay are QObject children of
  // `this` — they will be deleted automatically by ~QObject().

  qInfo() << "OverlayInstance: destroyed for profile:" << m_profileId;
}

// ============================================================================
// Lifecycle
// ============================================================================

void OverlayInstance::start() {
  if (m_running) {
    qWarning() << "OverlayInstance::start() called while already running,"
               << "profile:" << m_profileId;
    return;
  }

  qInfo() << "OverlayInstance: starting — profile:" << m_profileId
          << "mumbleLink:" << m_mumbleLinkName;

  // 1. Start MumbleLink polling (opens shared memory segment)
  //    Link name was set in constructor — start() only takes poll interval.
  m_mumbleLink->start(10);
  qInfo() << "[DEV] OverlayInstance: [1/7] MumbleLink started, segment:"
          << m_mumbleLinkName;

  // 2. Load per-profile marker preferences
  m_markerSettings->loadForProfile(m_profileId);
  qInfo() << "[DEV] OverlayInstance: [2/7] MarkerSettings loaded for:" << m_profileId;

  // 3. Wire shared MarkerController data into overlay components
  m_overlayWindow->setMarkerController(m_markerController);
  m_overlayWindow->setMarkerSettings(m_markerSettings);

  m_d3dOverlay->setMarkerManager(m_markerController->manager());
  m_d3dOverlay->setMarkerSettings(m_markerSettings);
  m_d3dOverlay->setImageCache(m_markerController->imageCache());

  // Phase 7a: Build per-instance query context
  m_queryContext.mapId = m_mumbleLink->mapId();
  m_queryContext.settings = m_markerSettings;
  m_queryContext.mumble = m_mumbleLink;

  // Pass query context to D3D11 pipelines and MinimapRenderer
  m_d3dOverlay->setQueryContext(&m_queryContext);
  m_minimapRenderer->setQueryContext(&m_queryContext);

  // Connect per-instance mapChanged → update context + ref-counted trail loading
  connect(m_mumbleLink, &MumbleLink::mapChanged,
          this, [this](uint32_t newMapId) {
            qInfo() << "[DEV] OverlayInstance: mapChanged —"
                    << "profile:" << m_profileId
                    << "oldMapId:" << m_queryContext.mapId
                    << "newMapId:" << newMapId;
            m_queryContext.mapId = newMapId;
            // Ref-counted trail loading (Phase 7b)
            auto *mgr = m_markerController->manager();
            if (m_lastMapId != 0 && m_lastMapId != newMapId) {
              mgr->releaseMap(m_lastMapId);
            }
            mgr->acquireMap(newMapId);
            m_lastMapId = newMapId;
          });

  qInfo() << "[DEV] OverlayInstance: [3/7] MarkerController wired + query context built"
          << "— mapId:" << m_queryContext.mapId;

  // 4. Per-instance MinimapRenderer: reparent into OverlayWindow and wire
  m_minimapRenderer->setParent(m_overlayWindow);
  m_minimapRenderer->setGeometry(0, 0, m_overlayWindow->width(),
                                 m_overlayWindow->height());
  m_minimapRenderer->lower(); // Draw below menu widget
  // Tell OverlayWindow about this renderer so resizeEvent keeps it sized
  m_overlayWindow->setMinimapRenderer(m_minimapRenderer);
  qInfo() << "[DEV] OverlayInstance: [4/7] MinimapRenderer reparented, size:"
          << m_overlayWindow->width() << "x" << m_overlayWindow->height();

  // Wire MumbleLink connection to start/stop MinimapRenderer
  // (replaces the removed global MumbleLink::connectionChanged →
  // MarkerController::setVisible connection from Phase 4)
  connect(m_mumbleLink, &MumbleLink::connectionChanged,
          this, [this](bool connected) {
            qInfo() << "[DEV] OverlayInstance: MumbleLink connectionChanged:"
                    << connected << "profile:" << m_profileId;
            if (connected) {
              m_minimapRenderer->start();
              qInfo() << "[DEV] OverlayInstance: MinimapRenderer started";
            } else {
              m_minimapRenderer->stop();
              qInfo() << "[DEV] OverlayInstance: MinimapRenderer stopped";
            }
          });

  // Wire marker settings to per-instance MinimapRenderer
  // (same pattern as MarkerController::setMarkerSettings() L116-133)
  auto syncMinimapSettings = [this]() {
    bool mainOn = m_markerSettings->renderingEnabled();
    m_minimapRenderer->setOpacity(
        static_cast<float>(m_markerSettings->minimapOpacity()));
    m_minimapRenderer->setMinimapMarkerScale(
        static_cast<float>(m_markerSettings->minimapMarkerScale()));
    m_minimapRenderer->setMinimapMarkerOpacity(
        static_cast<float>(m_markerSettings->minimapMarkerOpacity()));
    m_minimapRenderer->setShowMinimapMarkers(
        mainOn && m_markerSettings->renderMinimapEnabled());
    m_minimapRenderer->setShowBigMapMarkers(
        mainOn && m_markerSettings->renderBigMapEnabled());
    qInfo() << "[DEV] OverlayInstance: MinimapRenderer settings synced — rendering:"
            << mainOn << "minimap:" << m_markerSettings->renderMinimapEnabled()
            << "bigMap:" << m_markerSettings->renderBigMapEnabled()
            << "opacity:" << m_markerSettings->minimapOpacity();
  };
  connect(m_markerSettings, &MarkerSettingsManager::settingsChanged,
          this, syncMinimapSettings);
  syncMinimapSettings(); // Apply initial state

  // Keep proximity timer always enabled (shared MarkerManager).
  // Not toggled per-instance — one instance stopping must not disable
  // proximity for other running instances.
  m_markerController->manager()->setProximityEnabled(true);
  qInfo() << "[DEV] OverlayInstance: [5/7] MinimapRenderer wired + settings synced";

  // 5. Z-order coupling: D3D11 positions itself below the Qt overlay
  m_d3dOverlay->setQtOverlayHwnd(
      reinterpret_cast<HWND>(m_overlayWindow->winId()));
  qInfo() << "[DEV] OverlayInstance: [6/7] D3D11 Qt overlay HWND set";

  // 6. Start D3D11 tracking (deferred overlay creation inside)
  m_d3dOverlay->startTracking();
  qInfo() << "[DEV] OverlayInstance: [6/7] D3D11 tracking started";

  // 7. Cross-component signal bridge (4-arg connect for auto-disconnect)
  connect(m_overlayWindow, &OverlayWindow::detailsTrackerToggled,
          this, [this](bool visible) {
            m_d3dOverlay->toggleDebugOverlay(visible);
          });

  m_running = true;
  emit started(m_profileId);

  qInfo() << "[DEV] OverlayInstance: [7/7] started successfully — profile:"
          << m_profileId;
}

void OverlayInstance::stop() {
  if (!m_running) {
    return;
  }

  qInfo() << "OverlayInstance: stopping — profile:" << m_profileId;

  // Stop in reverse order: rendering first, then window tracking, then polling.
  // Explicit stop rather than relying on signal cascades from MumbleLink
  // disconnection — prevents race conditions during teardown.

  // 0. Release trail ref-count for this instance's current map (Phase 7b)
  if (m_lastMapId != 0) {
    m_markerController->manager()->releaseMap(m_lastMapId);
    m_lastMapId = 0;
  }

  // 1. Stop D3D11 rendering and window tracking
  m_d3dOverlay->stopTracking();

  // 2. Stop per-instance MinimapRenderer
  m_minimapRenderer->stop();

  // 3. Stop Qt overlay window tracking
  m_overlayWindow->stopTracking();

  // 3. Stop MumbleLink shared memory polling
  m_mumbleLink->stop();

  m_running = false;
  emit stopped(m_profileId);

  qInfo() << "OverlayInstance: stopped — profile:" << m_profileId;
}

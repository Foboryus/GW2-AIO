#include "ChildMinimap.h"

#include "core/AppConfig.h"
#include "core/MumbleLink.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerSettingsManager.h"
#include "features/markers/MinimapRenderer.h"
#include "ui/OverlayWindow.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

ChildMinimap::ChildMinimap(const QString &profileId,
                           const QString &mumbleName,
                           qint64 gw2Pid,
                           const QString &pipeName,
                           const QString &profileName,
                           QObject *parent)
    : ChildProcess(profileId, mumbleName, gw2Pid, pipeName, profileName, parent)
{
}

ChildMinimap::~ChildMinimap()
{
    // OverlayWindow is a top-level QWidget (no QObject parent) — delete explicitly
    delete m_overlayWindow;
    m_overlayWindow = nullptr;

    // MinimapRenderer was reparented to OverlayWindow — deleted with it
    m_minimapRenderer = nullptr;

    // MarkerQueryContext is a plain struct
    delete m_queryContext;
    m_queryContext = nullptr;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool ChildMinimap::onInitialize()
{
    qInfo() << "ChildMinimap: Initializing for" << profileName();

    // 1. Per-child MarkerSettingsManager
    const QString markerStateDir = AppConfig::instance().markerStateDir();
    m_markerSettings = new MarkerSettingsManager(markerStateDir, this);
    m_markerSettings->loadForProfile(profileId());
    qInfo() << "ChildMinimap: MarkerSettings loaded for" << profileId();

    // 2. Per-child ImageCache (marker icons)
    m_imageCache = new ImageCache(this);

    // 3. Per-child MarkerManager (shared marker data)
    m_markerManager = new MarkerManager(mumbleLink(), this);
    m_markerManager->setMarkerSettings(m_markerSettings);
    m_markerManager->setCacheDir(AppConfig::instance().markerPacksCacheDir());

    // 4. Per-instance query context
    m_queryContext = new MarkerQueryContext();
    m_queryContext->mapId = 0;
    m_queryContext->settings = m_markerSettings;
    m_queryContext->mumble = mumbleLink();

    // 5. Create transparent overlay window (tracks GW2 HWND position)
    //    headless=true: no menu widget, no zone editor — just HWND tracking
    m_overlayWindow = new OverlayWindow(mumbleLink(), nullptr, /*headless=*/true);
    m_overlayWindow->setClickThrough(true);  // Always click-through

    // 6. Create MinimapRenderer and embed in OverlayWindow
    m_minimapRenderer = new MinimapRenderer(
        m_markerManager, mumbleLink(), m_imageCache);
    m_minimapRenderer->setQueryContext(m_queryContext);
    m_minimapRenderer->setParent(m_overlayWindow);
    m_minimapRenderer->setGeometry(0, 0,
                                    m_overlayWindow->width(),
                                    m_overlayWindow->height());
    m_minimapRenderer->lower();
    m_overlayWindow->setMinimapRenderer(m_minimapRenderer);
    qInfo() << "ChildMinimap: MinimapRenderer created and embedded";

    // 7. Wire MumbleLink connection → start/stop MinimapRenderer
    connect(mumbleLink(), &MumbleLink::connectionChanged,
            this, [this](bool connected) {
                if (connected) {
                    m_minimapRenderer->start();
                    qInfo() << "ChildMinimap: MinimapRenderer started";
                } else {
                    m_minimapRenderer->stop();
                    qInfo() << "ChildMinimap: MinimapRenderer stopped";
                }
            });

    // 8. Wire settings changes to sync minimap display options
    connect(m_markerSettings, &MarkerSettingsManager::settingsChanged,
            this, &ChildMinimap::syncMinimapSettings);
    syncMinimapSettings();  // Apply initial state

    // 9. Start OverlayWindow tracking (finds GW2 HWND, installs WinEventHook)
    // Use guaranteed command-line PID to target the correct GW2 window.
    m_overlayWindow->setTargetPid(static_cast<uint32_t>(gw2Pid()));
    m_overlayWindow->startTracking();
    qInfo() << "ChildMinimap: OverlayWindow tracking started";

    return true;
}

void ChildMinimap::onShutdown()
{
    qInfo() << "ChildMinimap: Shutting down for" << profileName();

    if (m_minimapRenderer) {
        m_minimapRenderer->stop();
    }

    if (m_overlayWindow) {
        m_overlayWindow->stopTracking();
    }
}

// ============================================================================
// Map lifecycle
// ============================================================================

void ChildMinimap::onMapEntered(uint32_t mapId)
{
    qInfo() << "ChildMinimap: Map entered:" << mapId << "for" << profileName();

    // Load marker packs on first map entry
    if (!m_packsLoaded) {
        qInfo() << "ChildMinimap: Loading marker packs...";
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        m_packsLoaded = true;
        qInfo() << "ChildMinimap: Packs loaded, count:"
                << m_markerManager->packs().size();
    }

    // Update query context
    m_queryContext->mapId = mapId;

    // Load trail data for this map
    m_markerManager->acquireMap(mapId);

    // Enable proximity checking
    m_markerManager->setProximityEnabled(true);

    // Ensure renderer is visible
    m_minimapRenderer->setRenderingEnabled(true);
}

void ChildMinimap::onMapLeft()
{
    qInfo() << "ChildMinimap: Map left for" << profileName();

    uint32_t oldMapId = m_queryContext->mapId;
    m_queryContext->mapId = 0;

    if (oldMapId > 0) {
        m_markerManager->releaseMap(oldMapId);
    }

    m_markerManager->setProximityEnabled(false);
    m_minimapRenderer->setRenderingEnabled(false);
}

// ============================================================================
// Focus
// ============================================================================

void ChildMinimap::onFocusChanged(bool focused)
{
    qInfo() << "ChildMinimap: Focus" << (focused ? "gained" : "lost")
            << "for" << profileName();

    if (m_minimapRenderer) {
        m_minimapRenderer->setRenderingEnabled(focused && isInGame());
    }
}

// ============================================================================
// Settings
// ============================================================================

void ChildMinimap::onSettingsReceived(const QJsonObject &settings)
{
    qInfo() << "ChildMinimap: Settings received";

    // --- Rendering toggles ---
    if (settings.contains("renderingEnabled")) {
        m_markerSettings->setRenderingEnabled(
            settings["renderingEnabled"].toBool(true));
    }
    if (settings.contains("renderMinimapEnabled")) {
        m_markerSettings->setRenderMinimapEnabled(
            settings["renderMinimapEnabled"].toBool(true));
    }
    if (settings.contains("renderBigMapEnabled")) {
        m_markerSettings->setRenderBigMapEnabled(
            settings["renderBigMapEnabled"].toBool(true));
    }

    // --- Minimap display settings ---
    if (settings.contains("minimapOpacity")) {
        m_markerSettings->setMinimapOpacity(
            settings["minimapOpacity"].toDouble(1.0));
    }
    if (settings.contains("minimapMarkerScale")) {
        m_markerSettings->setMinimapMarkerScale(
            settings["minimapMarkerScale"].toDouble(1.0));
    }
    if (settings.contains("minimapMarkerOpacity")) {
        m_markerSettings->setMinimapMarkerOpacity(
            settings["minimapMarkerOpacity"].toDouble(1.0));
    }
    if (settings.contains("minimapTrailWidth")) {
        m_markerSettings->setMinimapTrailWidth(
            static_cast<float>(settings["minimapTrailWidth"].toDouble(1.0)));
    }

    // --- Height filter ---
    if (settings.contains("heightFilterEnabled")) {
        m_markerSettings->setHeightFilterEnabled(
            settings["heightFilterEnabled"].toBool());
    }
    if (settings.contains("heightFilterRange")) {
        m_markerSettings->setHeightFilterRange(
            static_cast<float>(settings["heightFilterRange"].toDouble()));
    }

    // syncMinimapSettings will be called via settingsChanged signal
}

void ChildMinimap::syncMinimapSettings()
{
    if (!m_minimapRenderer || !m_markerSettings) return;

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

    qInfo() << "ChildMinimap: Settings synced — rendering:" << mainOn
            << "minimap:" << m_markerSettings->renderMinimapEnabled()
            << "bigMap:" << m_markerSettings->renderBigMapEnabled();
}

void ChildMinimap::onReloadPacks()
{
    qInfo() << "ChildMinimap: Reloading pack data from disk";

    // 1. Reload settings (pack enabled/disabled state)
    if (m_markerSettings) {
        m_markerSettings->loadForProfile(profileId());
    }

    // 2. Re-parse all pack archives — previously disabled packs were loaded
    //    as metadata-only. Full re-parse picks up actual marker/trail data.
    if (m_markerManager) {
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        qInfo() << "ChildMinimap: Packs reloaded, count:"
                << m_markerManager->packs().size();

        // 3. Re-acquire current map to rebuild map-specific data
        if (m_queryContext && m_queryContext->mapId > 0) {
            m_markerManager->acquireMap(m_queryContext->mapId);
        }
    }
}

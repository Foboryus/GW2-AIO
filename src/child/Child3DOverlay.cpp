#include "Child3DOverlay.h"

#include "core/AppConfig.h"
#include "core/MumbleLink.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerSettingsManager.h"
#include "rendering/D3D11OverlayWindow.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

Child3DOverlay::Child3DOverlay(const QString &profileId,
                               const QString &mumbleName,
                               qint64 gw2Pid,
                               const QString &pipeName,
                               const QString &profileName,
                               QObject *parent)
    : ChildProcess(profileId, mumbleName, gw2Pid, pipeName, profileName, parent)
{
}

Child3DOverlay::~Child3DOverlay()
{
    // D3D11OverlayWindow, MarkerManager, MarkerSettingsManager, ImageCache
    // are QObject children of `this` — cleaned up automatically.
    // MarkerQueryContext is a plain struct — delete it manually.
    delete m_queryContext;
    m_queryContext = nullptr;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool Child3DOverlay::onInitialize()
{
    qInfo() << "Child3DOverlay: Initializing for" << profileName();

    // 1. Create per-child MarkerSettingsManager
    //    Uses AppConfig::markerStateDir() (→ ProfileData/marker_state)
    const QString markerStateDir = AppConfig::instance().markerStateDir();
    m_markerSettings = new MarkerSettingsManager(markerStateDir, this);
    m_markerSettings->loadForProfile(profileId());
    qInfo() << "Child3DOverlay: MarkerSettings loaded for" << profileId();

    // 2. Create per-child ImageCache (textures loaded on demand)
    m_imageCache = new ImageCache(this);

    // 3. Create per-child MarkerManager
    //    Uses this process's own MumbleLink instance (from ChildProcess base)
    m_markerManager = new MarkerManager(mumbleLink(), this);
    m_markerManager->setMarkerSettings(m_markerSettings);
    m_markerManager->setCacheDir(AppConfig::instance().markerPacksCacheDir());

    // 4. Create per-instance query context
    m_queryContext = new MarkerQueryContext();
    m_queryContext->mapId = 0;
    m_queryContext->settings = m_markerSettings;
    m_queryContext->mumble = mumbleLink();

    // 5. Create D3D11 overlay window
    m_d3dOverlay = new D3D11OverlayWindow(mumbleLink(), this);
    m_d3dOverlay->setMarkerManager(m_markerManager);
    m_d3dOverlay->setMarkerSettings(m_markerSettings);
    m_d3dOverlay->setImageCache(m_imageCache);
    m_d3dOverlay->setQueryContext(m_queryContext);
    m_d3dOverlay->setClickThrough(true);  // Always click-through (no Qt HUD)
    m_d3dOverlay->setHideOnUnfocus(true); // Hide when GW2 loses focus (multibox)

    // 6. Connect focus changes from D3D11 overlay
    connect(m_d3dOverlay, &D3D11OverlayWindow::focusChanged,
            this, [this](bool focused) {
                qInfo() << "Child3DOverlay: D3D11 focusChanged:" << focused;
                // Don't duplicate — ChildProcess handles focus via MumbleLink
            });

    // 7. Start D3D11 tracking (deferred window creation inside)
    // Use guaranteed command-line PID to target the correct GW2 window.
    m_d3dOverlay->setTargetPid(static_cast<uint32_t>(gw2Pid()));
    m_d3dOverlay->startTracking();
    qInfo() << "Child3DOverlay: D3D11 tracking started";

    // Packs are NOT loaded yet — they load on first map enter (onMapEntered)

    return true;
}

void Child3DOverlay::onShutdown()
{
    qInfo() << "Child3DOverlay: Shutting down for" << profileName();

    if (m_d3dOverlay) {
        m_d3dOverlay->stopTracking();
    }
}

// ============================================================================
// Map lifecycle
// ============================================================================

void Child3DOverlay::onMapEntered(uint32_t mapId)
{
    qInfo() << "Child3DOverlay: Map entered:" << mapId
            << "for" << profileName();

    // Load marker packs on first map entry (deferred from init for speed)
    if (!m_packsLoaded) {
        qInfo() << "Child3DOverlay: Loading marker packs...";
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        m_packsLoaded = true;
        qInfo() << "Child3DOverlay: Packs loaded, count:"
                << m_markerManager->packs().size();
    }

    // Update the query context with the new map ID
    m_queryContext->mapId = mapId;

    // Ensure trail data is loaded for this map (lazy per-map loading)
    m_markerManager->acquireMap(mapId);

    // Enable proximity checking
    m_markerManager->setProximityEnabled(true);

    // Enable rendering
    m_d3dOverlay->setRenderingEnabled(true);
}

void Child3DOverlay::onMapLeft()
{
    qInfo() << "Child3DOverlay: Map left for" << profileName();

    uint32_t oldMapId = m_queryContext->mapId;

    // Update context
    m_queryContext->mapId = 0;

    // Release trail ref-count for the old map
    if (oldMapId > 0) {
        m_markerManager->releaseMap(oldMapId);
    }

    // Disable proximity checking (no map → no markers)
    m_markerManager->setProximityEnabled(false);

    // Pause rendering (no map data to render)
    m_d3dOverlay->setRenderingEnabled(false);
}

// ============================================================================
// Focus
// ============================================================================

void Child3DOverlay::onFocusChanged(bool focused)
{
    qInfo() << "Child3DOverlay: Focus" << (focused ? "gained" : "lost")
            << "for" << profileName();

    if (m_d3dOverlay) {
        m_d3dOverlay->setRenderingEnabled(focused && isInGame());
    }
}

// ============================================================================
// Settings from grandfather (via pipe)
// ============================================================================

void Child3DOverlay::onSettingsReceived(const QJsonObject &settings)
{
    qInfo() << "Child3DOverlay: Received settings update";

    // --- Rendering toggles ---
    if (settings.contains("renderingEnabled")) {
        m_markerSettings->setRenderingEnabled(
            settings["renderingEnabled"].toBool(true));
    }
    if (settings.contains("render3dEnabled")) {
        m_markerSettings->setRender3dEnabled(
            settings["render3dEnabled"].toBool(true));
    }

    // --- 3D display settings (read by D3D11OverlayWindow each frame) ---
    if (settings.contains("overlayOpacity")) {
        m_markerSettings->setOverlayOpacity(
            settings["overlayOpacity"].toDouble(1.0));
    }
    if (settings.contains("maxRenderDistance")) {
        m_markerSettings->setMaxRenderDistance(
            settings["maxRenderDistance"].toDouble(4000.0));
    }
    if (settings.contains("markerScale")) {
        m_markerSettings->setMarkerScale(
            settings["markerScale"].toDouble(1.0));
    }
    if (settings.contains("showDistance")) {
        m_markerSettings->setShowDistance(
            settings["showDistance"].toBool(false));
    }
    if (settings.contains("distanceFontSize")) {
        m_markerSettings->setDistanceFontSize(
            settings["distanceFontSize"].toInt(12));
    }
    if (settings.contains("distanceLabelOffset")) {
        m_markerSettings->setDistanceLabelOffset(
            settings["distanceLabelOffset"].toInt(0));
    }

    // --- Height filter (applied on MarkerManager) ---
    if (settings.contains("heightFilterEnabled")) {
        m_markerManager->setHeightFilterEnabled(
            settings["heightFilterEnabled"].toBool());
    }
    if (settings.contains("heightFilterRange")) {
        m_markerManager->setHeightFilterRange(
            static_cast<float>(settings["heightFilterRange"].toDouble()));
    }

    // --- Exclusion zones ---
    if (settings.contains("exclusionEnabled")) {
        m_markerSettings->setExclusionEnabled(
            settings["exclusionEnabled"].toBool(false));
    }
    if (settings.contains("minimapZoneEnabled")) {
        m_markerSettings->setMinimapZoneEnabled(
            settings["minimapZoneEnabled"].toBool(false));
    }
    if (settings.contains("skillBarZoneEnabled")) {
        m_markerSettings->setSkillBarZoneEnabled(
            settings["skillBarZoneEnabled"].toBool(false));
    }
    if (settings.contains("chatZoneEnabled")) {
        m_markerSettings->setChatZoneEnabled(
            settings["chatZoneEnabled"].toBool(false));
    }
    if (settings.contains("exclusionFadeEdge")) {
        m_markerSettings->setExclusionFadeEdge(
            static_cast<float>(settings["exclusionFadeEdge"].toDouble()));
    }

    qInfo() << "Child3DOverlay: Settings applied";
}

void Child3DOverlay::onReloadPacks()
{
    qInfo() << "Child3DOverlay: Reloading pack data from disk";

    // 1. Reload settings (pack enabled/disabled state)
    if (m_markerSettings) {
        m_markerSettings->loadForProfile(profileId());
    }

    // 2. Re-parse all pack archives — previously disabled packs were loaded
    //    as metadata-only (no markers/trails). Full re-parse picks up the
    //    actual data now that the pack may be enabled.
    if (m_markerManager) {
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        qInfo() << "Child3DOverlay: Packs reloaded, count:"
                << m_markerManager->packs().size();

        // 3. Re-acquire current map to rebuild map-specific data
        if (m_queryContext && m_queryContext->mapId > 0) {
            m_markerManager->acquireMap(m_queryContext->mapId);
        }
    }
}

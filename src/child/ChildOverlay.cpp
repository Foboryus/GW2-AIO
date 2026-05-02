#include "ChildOverlay.h"

#include "core/AppConfig.h"
#include "core/MumbleLink.h"
#include "core/RadialSettingsManager.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerSettingsManager.h"
#include "ui/OverlayWindow.h"
#include "ui/OverlayMenuWidget.h"
#include "core/OverlayZOrder.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

ChildOverlay::ChildOverlay(const QString &profileId,
                           const QString &mumbleName,
                           qint64 gw2Pid,
                           const QString &pipeName,
                           const QString &profileName,
                           QObject *parent)
    : ChildProcess(profileId, mumbleName, gw2Pid, pipeName, profileName, parent)
{
}

ChildOverlay::~ChildOverlay()
{
    // OverlayWindow is a top-level QWidget (no QObject parent)
    delete m_overlayWindow;
    m_overlayWindow = nullptr;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool ChildOverlay::onInitialize()
{
    qInfo() << "ChildOverlay: Initializing for" << profileName();

    // 1. Per-child MarkerSettingsManager
    const QString markerStateDir = AppConfig::instance().markerStateDir();
    m_markerSettings = new MarkerSettingsManager(markerStateDir, this);
    m_markerSettings->loadForProfile(profileId());

    // 2. Per-child ImageCache (for overlay menu icon rendering)
    m_imageCache = new ImageCache(this);

    // 3. Per-child MarkerManager (lightweight — no D3D11)
    //    Used by OverlayMenuWidget for pack/category tree
    m_markerManager = new MarkerManager(mumbleLink(), this);
    m_markerManager->setMarkerSettings(m_markerSettings);
    m_markerManager->setCacheDir(AppConfig::instance().markerPacksCacheDir());

    // 4. Create OverlayWindow in FULL mode (with menu widget)
    m_overlayWindow = new OverlayWindow(mumbleLink());
    m_overlayWindow->setMarkerManager(m_markerManager);
    m_overlayWindow->setMarkerSettings(m_markerSettings);
    m_overlayWindow->setZOrderLayer(OverlayZOrder::kLayerHUD);

    // 5. Start tracking GW2 window (HWND, position, z-order)
    // Use guaranteed command-line PID to target the correct GW2 window.
    m_overlayWindow->setTargetPid(static_cast<uint32_t>(gw2Pid()));
    m_overlayWindow->startTracking();
    qInfo() << "ChildOverlay: OverlayWindow tracking started";

    // 5b. Load radial settings and pass to overlay menu
    {
        const QString radialConfigDir = AppConfig::instance().radialConfigDir();
        RadialSettingsManager radialMgr(radialConfigDir);
        radialMgr.loadForProfile(profileId());
        m_radialEnabled = radialMgr.settings().radialEnabled;

        // Pass to the overlay menu widget
        if (m_overlayWindow && m_overlayWindow->overlayMenu()) {
            m_overlayWindow->overlayMenu()->setRadialEnabled(m_radialEnabled);
        }
    }

    // 6. Wire settings changes to upstream IPC
    // When user changes settings in the overlay menu, push them to grandfather
    // for relay to sibling children (3d, minimap)
    connect(m_markerSettings, &MarkerSettingsManager::settingsChanged,
            this, [this]() {
        QJsonObject settings;
        settings["renderingEnabled"] = m_markerSettings->renderingEnabled();
        settings["render3dEnabled"] = m_markerSettings->render3dEnabled();
        settings["renderMinimapEnabled"] = m_markerSettings->renderMinimapEnabled();
        settings["renderBigMapEnabled"] = m_markerSettings->renderBigMapEnabled();
        settings["overlayOpacity"] = m_markerSettings->overlayOpacity();
        settings["minimapOpacity"] = m_markerSettings->minimapOpacity();
        settings["markerScale"] = m_markerSettings->markerScale();
        settings["minimapTrailWidth"] = static_cast<double>(m_markerSettings->minimapTrailWidth());
        settings["minimapMarkerScale"] = m_markerSettings->minimapMarkerScale();
        settings["minimapMarkerOpacity"] = m_markerSettings->minimapMarkerOpacity();
        settings["maxRenderDistance"] = m_markerSettings->maxRenderDistance();
        settings["showDistance"] = m_markerSettings->showDistance();
        settings["distanceFontSize"] = m_markerSettings->distanceFontSize();
        settings["distanceLabelOffset"] = m_markerSettings->distanceLabelOffset();
        settings["hideInCombat"] = m_markerSettings->hideInCombat();
        settings["showInBigMap"] = m_markerSettings->showInBigMap();
        settings["exclusionEnabled"] = m_markerSettings->exclusionEnabled();
        settings["minimapZoneEnabled"] = m_markerSettings->minimapZoneEnabled();
        settings["skillBarZoneEnabled"] = m_markerSettings->skillBarZoneEnabled();
        settings["chatZoneEnabled"] = m_markerSettings->chatZoneEnabled();
        settings["heightFilterEnabled"] = m_markerSettings->heightFilterEnabled();
        settings["heightFilterRange"] = static_cast<double>(m_markerSettings->heightFilterRange());
        settings["exclusionFadeEdge"] = static_cast<double>(m_markerSettings->exclusionFadeEdge());

        QJsonDocument doc(settings);
        QByteArray payload = "SETTING_CHANGED\n" + doc.toJson(QJsonDocument::Compact);
        sendToGrandfather(payload);
    });

    // 6b. Wire radial toggle from overlay menu → upstream IPC
    if (m_overlayWindow && m_overlayWindow->overlayMenu()) {
        connect(m_overlayWindow->overlayMenu(), &OverlayMenuWidget::radialToggleChanged,
                this, [this](bool enabled) {
            m_radialEnabled = enabled;

            // Save to radial settings file
            const QString radialConfigDir = AppConfig::instance().radialConfigDir();
            RadialSettingsManager radialMgr(radialConfigDir);
            radialMgr.loadForProfile(profileId());
            RadialSettings rs = radialMgr.settings();
            rs.radialEnabled = enabled;
            radialMgr.setSettings(rs);
            radialMgr.saveForProfile(profileId());

            // Send upstream so grandfather calls syncFeatureToggles
            QJsonObject msg;
            msg["radialEnabled"] = enabled;
            QJsonDocument doc(msg);
            QByteArray payload = "RADIAL_TOGGLE\n" + doc.toJson(QJsonDocument::Compact);
            sendToGrandfather(payload);

            qInfo() << "ChildOverlay: Radial toggle →" << enabled
                    << "— saved + sent upstream";
        });
    }

    // 7. When pack/category data is saved to disk (debounced 2s), tell
    //    grandfather so siblings can reload from disk
    connect(m_markerSettings, &MarkerSettingsManager::saved,
            this, [this]() {
        sendToGrandfather("RELOAD_PACKS");
        qInfo() << "ChildOverlay: Sent RELOAD_PACKS to grandfather";
    });
    // 8. Layer 2 focus: wire overlay's WinEvent focus to base class
    //    for instant focus detection (bypasses MumbleLink polling)
    connect(m_overlayWindow, &OverlayWindow::gameFocusChanged,
            this, &ChildOverlay::notifyOverlayFocusChanged);

    qInfo() << "[DEV][OVERLAY] Init complete:"
            << "overlayWindow:" << (m_overlayWindow != nullptr)
            << "markerManager:" << (m_markerManager != nullptr)
            << "targetPid:" << gw2Pid()
            << "zOrder:" << OverlayZOrder::kLayerHUD;

    return true;
}

void ChildOverlay::onShutdown()
{
    qInfo() << "ChildOverlay: Shutting down for" << profileName();

    if (m_overlayWindow) {
        m_overlayWindow->stopTracking();
    }
}

// ============================================================================
// Map lifecycle
// ============================================================================

void ChildOverlay::onMapEntered(uint32_t mapId)
{
    qInfo() << "ChildOverlay: Map entered:" << mapId << "for" << profileName();

    // Load packs on first map entry (deferred for startup speed)
    if (!m_packsLoaded) {
        qInfo() << "ChildOverlay: Loading marker packs for menu tree...";
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        m_packsLoaded = true;
        qInfo() << "ChildOverlay: Packs loaded:" << m_markerManager->packs().size();
    }

    // Acquire trail data for this map (needed for menu category display)
    m_markerManager->acquireMap(mapId);
}

void ChildOverlay::onMapLeft()
{
    qInfo() << "ChildOverlay: Map left for" << profileName();
    // Release trail data — not needed when not in-game
}

// ============================================================================
// Focus
// ============================================================================

void ChildOverlay::onFocusChanged(bool focused)
{
    qInfo() << "[DIAG] ChildOverlay: FOCUS_CHANGED"
            << profileName()
            << "focused:" << focused
            << "inGame:" << isInGame();

    if (m_overlayWindow) {
        qInfo() << "[DEV][OVERLAY] Window state:"
                << "visible:" << m_overlayWindow->isVisible()
                << "size:" << m_overlayWindow->size()
                << "pos:" << m_overlayWindow->pos();
    }
    // OverlayWindow handles its own visibility via MumbleLink signals
    // No additional action needed here
}

// ============================================================================
// Settings from grandfather (via pipe)
// ============================================================================

void ChildOverlay::onSettingsReceived(const QJsonObject &settings)
{
    qInfo() << "ChildOverlay: Settings received";

    // Apply rendering toggle settings (sync from grandfather)
    if (settings.contains("renderingEnabled")) {
        m_markerSettings->setRenderingEnabled(
            settings["renderingEnabled"].toBool(true));
    }

    // Radial enabled state (from sibling SETTING_CHANGED relay)
    if (settings.contains("radialEnabled")) {
        m_radialEnabled = settings["radialEnabled"].toBool(true);
        if (m_overlayWindow && m_overlayWindow->overlayMenu()) {
            m_overlayWindow->overlayMenu()->setRadialEnabled(m_radialEnabled);
        }
    }
}

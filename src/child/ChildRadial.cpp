/**
 * @file ChildRadial.cpp
 * @brief Radial menu child process implementation
 *
 * Creates a RadialController that manages the D3D11 overlay window
 * and radial wheel rendering for the associated GW2 instance.
 *
 * Settings flow:
 *   1. onInitialize(): Load RadialSettings from disk → pass to controller
 *   2. onSettingsReceived(): Parse IPC JSON → update controller live
 */

#include "ChildRadial.h"
#include "RadialController.h"
#include "RadialOverlayWindow.h"

#include "core/AppConfig.h"
#include "core/RadialSettingsManager.h"

#include <QJsonDocument>

ChildRadial::ChildRadial(const QString &profileId,
                         const QString &mumbleName,
                         qint64 gw2Pid,
                         const QString &pipeName,
                         const QString &profileName,
                         QObject *parent)
    : ChildProcess(profileId, mumbleName, gw2Pid, pipeName, profileName, parent)
{
}

bool ChildRadial::onInitialize()
{
    qInfo() << "ChildRadial: Initializing D3D11 radial overlay for"
            << profileName();

    // 1. Load per-profile radial settings from disk
    //    Pattern follows ChildOverlay → MarkerSettingsManager
    const QString radialDir = AppConfig::instance().radialConfigDir();
    m_radialSettings = new RadialSettingsManager(radialDir, this);
    m_radialSettings->loadForProfile(profileId());

    qInfo() << "ChildRadial: Settings loaded — enabled:"
            << m_radialSettings->settings().radialEnabled
            << "mountHotkey:" << m_radialSettings->settings().mountHotkey
            << "mounts:" << m_radialSettings->settings().mounts.size()
            << "wheelScale:" << m_radialSettings->settings().wheelScale;

    // 2. Create the radial controller with MumbleLink and target PID
    m_controller = new RadialController(
        mumbleLink(), static_cast<uint32_t>(gw2Pid()), this);

    // 3. Apply loaded settings to controller (replaces hardcoded defaults)
    m_controller->applySettings(m_radialSettings->settings());

    // 4. Start the D3D11 overlay — will track the GW2 window
    m_controller->start();

    // 5. Layer 2 focus: wire overlay's WinEvent focus to base class
    //    for instant focus detection (bypasses MumbleLink polling)
    connect(m_controller->overlayWindow(), &RadialOverlayWindow::focusChanged,
            this, &ChildRadial::notifyOverlayFocusChanged);

    qInfo() << "ChildRadial: Initialized successfully";
    return true;
}

void ChildRadial::onShutdown()
{
    qInfo() << "ChildRadial: Shutting down for" << profileName();

    if (m_controller) {
        m_controller->stop();
    }
}

void ChildRadial::onMapEntered(uint32_t mapId)
{
    Q_UNUSED(mapId);
    // Radial menu doesn't care about map changes
}

void ChildRadial::onMapLeft()
{
    // Radial menu doesn't care about map changes
}

void ChildRadial::onFocusChanged(bool focused)
{
    qInfo() << "[DIAG] ChildRadial: FOCUS_CHANGED"
            << profileName()
            << "focused:" << focused
            << "inGame:" << isInGame();

    if (m_controller) {
        m_controller->onFocusChanged(focused);
    }
}

void ChildRadial::onSettingsReceived(const QJsonObject &settings)
{
    qInfo() << "ChildRadial: Settings received via IPC for" << profileName();

    // Parse and apply radial settings if present
    if (settings.contains("radialSettings")) {
        QJsonObject radialJson = settings["radialSettings"].toObject();
        RadialSettings newSettings = RadialSettings::fromJson(radialJson);

        qInfo() << "ChildRadial: Applying IPC settings — enabled:"
                << newSettings.radialEnabled
                << "mountHotkey:" << newSettings.mountHotkey
                << "wheelScale:" << newSettings.wheelScale;

        if (m_controller) {
            m_controller->applySettings(newSettings);
        }
    }
}


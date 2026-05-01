/**
 * @file ChildRadial.cpp
 * @brief Radial menu child process — SharedTexture rendering
 *
 * Creates a RadialController that manages D3D11 radial wheel rendering.
 * Output goes to SharedTextureProducer instead of a swap chain overlay.
 * Hotkey polling runs on every MumbleLink tick via the controller.
 */

#include "ChildRadial.h"
#include "RadialController.h"

#include "core/AppConfig.h"
#include "core/MumbleLink.h"
#include "core/RadialSettingsManager.h"
#include "rendering/D3D11Context.h"
#include "rendering/SharedTexture.h"

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

ChildRadial::~ChildRadial()
{
    if (m_controller) {
        m_controller->stop();
        delete m_controller;
        m_controller = nullptr;
    }

    if (m_sharedTexture) {
        m_sharedTexture->shutdown();
        delete m_sharedTexture;
        m_sharedTexture = nullptr;
    }

    if (m_d3dContext) {
        m_d3dContext->shutdown();
        delete m_d3dContext;
        m_d3dContext = nullptr;
    }
}

bool ChildRadial::onInitialize()
{
    qInfo() << "ChildRadial: Initializing D3D11 radial overlay for"
            << profileName();

    // 1. Load per-profile radial settings from disk
    const QString radialDir = AppConfig::instance().radialConfigDir();
    m_radialSettings = new RadialSettingsManager(radialDir, this);
    m_radialSettings->loadForProfile(profileId());

    qInfo() << "ChildRadial: Settings loaded — enabled:"
            << m_radialSettings->settings().radialEnabled
            << "mountHotkey:" << m_radialSettings->settings().mountHotkey
            << "mounts:" << m_radialSettings->settings().mounts.size()
            << "wheelScale:" << m_radialSettings->settings().wheelScale;

    // 2. Find GW2 window for dimensions
    findGW2Window();
    int initW = m_gw2Width > 0 ? m_gw2Width : 800;
    int initH = m_gw2Height > 0 ? m_gw2Height : 600;

    // 3. D3D11 offscreen context
    m_d3dContext = new D3D11Context();
    if (!m_d3dContext->initializeOffscreen(QSize(initW, initH))) {
        qCritical() << "ChildRadial: D3D11 offscreen init failed";
        return false;
    }
    qInfo() << "ChildRadial: D3D11 offscreen context initialized"
            << initW << "x" << initH;

    // 4. SharedTextureProducer
    m_sharedTexture = new SharedTextureProducer();
    QString texName = QStringLiteral("GW2AIO_Tex_%1_radial").arg(profileId());
    if (!m_sharedTexture->initialize(
            m_d3dContext->device(), m_d3dContext->context(),
            initW, initH, texName)) {
        qCritical() << "ChildRadial: SharedTexture init failed";
        return false;
    }
    qInfo() << "ChildRadial: SharedTexture created:" << texName
            << initW << "x" << initH;

    // 5. Create controller (hotkey polling + wheel state + rendering)
    m_controller = new RadialController(
        mumbleLink(), static_cast<uint32_t>(gw2Pid()), this);
    m_controller->applySettings(m_radialSettings->settings());

    // 6. Provide the D3D11 context to the controller for renderer init
    m_controller->setD3DContext(m_d3dContext);

    // 7. Wire MumbleLink::dataUpdated → render tick (hotkey polling + rendering)
    connect(mumbleLink(), &MumbleLink::dataUpdated,
            this, &ChildRadial::onRenderTick, Qt::UniqueConnection);

    // 8. Start the controller (without overlay window)
    m_controller->startHeadless();

    qInfo() << "[DEV][RADIAL] Init complete:"
            << "controller:" << (m_controller != nullptr)
            << "sharedTex:" << (m_sharedTexture != nullptr)
            << "targetPid:" << gw2Pid()
            << "enabled:" << m_radialSettings->settings().radialEnabled
            << "mounts:" << m_radialSettings->settings().mounts.size();
    return true;
}

void ChildRadial::onShutdown()
{
    qInfo() << "ChildRadial: Shutting down for" << profileName();

    if (m_controller) {
        m_controller->stop();
    }

    if (m_sharedTexture) {
        m_sharedTexture->shutdown();
    }
}

// ============================================================================
// Render Tick
// ============================================================================

void ChildRadial::onRenderTick()
{
    if (!m_controller || !m_d3dContext || !m_sharedTexture) return;

    // Always poll hotkey (even when unfocused — controller gates internally)
    m_controller->pollHotkey();

    // Only render when wheel is active or fading
    if (!m_controller->needsRendering()) return;

    // Poll GW2 window size
    pollGW2WindowSize();
    if (m_gw2Width <= 0 || m_gw2Height <= 0) return;

    // Resize if needed
    if (m_sharedTexture->width() != m_gw2Width ||
        m_sharedTexture->height() != m_gw2Height) {
        m_sharedTexture->resize(m_gw2Width, m_gw2Height);
        qInfo() << "[DEV][RADIAL] Resized to" << m_gw2Width << "x" << m_gw2Height;
    }

    // Acquire shared texture for writing
    ID3D11RenderTargetView *rtv = m_sharedTexture->acquireForWrite(0);
    if (!rtv) return;

    // Use setExternalRTV + beginFrame to set all required D3D11 state
    // (blend state, rasterizer state, viewport, clear) — RadialRenderer
    // depends on alpha blending being enabled by beginFrame()
    m_d3dContext->setExternalRTV(rtv);
    m_d3dContext->beginFrame();

    // Get cursor position relative to GW2 window
    POINT cursor = {};
    GetCursorPos(&cursor);
    if (m_gw2Hwnd && IsWindow(m_gw2Hwnd)) {
        ScreenToClient(m_gw2Hwnd, &cursor);
    }

    // Render the wheel
    m_controller->renderToTarget(m_d3dContext, cursor.x, cursor.y,
                                  m_gw2Width, m_gw2Height);

    // Flush and release
    m_d3dContext->context()->Flush();
    m_sharedTexture->releaseAfterWrite();
}

// ============================================================================
// GW2 Window
// ============================================================================

bool ChildRadial::findGW2Window()
{
    if (m_gw2Hwnd && IsWindow(m_gw2Hwnd)) return true;

    DWORD targetPid = static_cast<DWORD>(gw2Pid());
    struct EnumData { DWORD pid; HWND result; };
    EnumData data = {targetPid, nullptr};

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lParam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != d->pid) return TRUE;
        if (!IsWindowVisible(hwnd)) return TRUE;

        wchar_t cls[64] = {};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"ArenaNet_Dx_Window_Class") == 0 ||
            wcscmp(cls, L"ArenaNet_Gr_Window_Class") == 0) {
            d->result = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    if (data.result) {
        m_gw2Hwnd = data.result;
        pollGW2WindowSize();
        qInfo() << "[DEV][RADIAL] Found GW2 HWND:" << m_gw2Hwnd
                << "size:" << m_gw2Width << "x" << m_gw2Height;
        return true;
    }
    return false;
}

void ChildRadial::pollGW2WindowSize()
{
    if (!m_gw2Hwnd || !IsWindow(m_gw2Hwnd)) {
        m_gw2Hwnd = nullptr;
        findGW2Window();
        return;
    }

    RECT rect = {};
    GetClientRect(m_gw2Hwnd, &rect);
    m_gw2Width = rect.right - rect.left;
    m_gw2Height = rect.bottom - rect.top;
}

// ============================================================================
// Map lifecycle (radial doesn't care)
// ============================================================================

void ChildRadial::onMapEntered(uint32_t mapId)
{
    Q_UNUSED(mapId);
}

void ChildRadial::onMapLeft()
{
}

// ============================================================================
// Focus
// ============================================================================

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

// ============================================================================
// Settings
// ============================================================================

void ChildRadial::onSettingsReceived(const QJsonObject &settings)
{
    qInfo() << "ChildRadial: Settings received via IPC for" << profileName();

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

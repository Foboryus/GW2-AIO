/**
 * @file ChildRadial.cpp
 * @brief Radial menu child process — SharedTexture rendering
 *
 * Creates a RadialController that manages D3D11 radial wheel rendering.
 * Output goes to SharedTextureProducer instead of a swap chain overlay.
 * Hotkey polling runs on every MumbleLink tick via the controller.
 *
 * Rendering pattern (matches Child3DOverlay):
 *   1. Acquire SharedTexture mutex
 *   2. Set intermediate RT as external RTV
 *   3. beginFrame() → sets blend state, rasterizer, viewport, clear
 *   4. renderToTarget() → draws wheel, elements, cursor
 *   5. CopyResource → intermediate RT to SharedTexture
 *   6. Release mutex
 */

#include "ChildRadial.h"
#include "RadialController.h"

#include "core/AppConfig.h"
#include "core/MumbleLink.h"
#include "core/RadialSettingsManager.h"
#include "rendering/D3D11Context.h"
#include "rendering/SharedTexture.h"

#include <QJsonDocument>
#include <QDateTime>

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

    m_intermediateRTV.Reset();
    m_intermediateRT.Reset();

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
    qInfo() << "ChildRadial: Initializing for" << profileName();

    // 1. Load per-profile radial settings from disk
    const QString radialDir = AppConfig::instance().radialConfigDir();
    m_radialSettings = new RadialSettingsManager(radialDir, this);
    m_radialSettings->loadForProfile(profileId());

    qInfo() << "ChildRadial: Settings loaded — enabled:"
            << m_radialSettings->settings().radialEnabled
            << "mountHotkey:" << m_radialSettings->settings().mountHotkey
            << "mounts:" << m_radialSettings->settings().mounts.size()
            << "wheelScale:" << m_radialSettings->settings().wheelScale;

    // 2. Find GW2 window for dimensions (needed by controller)
    findGW2Window();

    // 3. Create controller (hotkey polling + wheel state)
    //    Controller can poll hotkeys without D3D11 — rendering is guarded
    m_controller = new RadialController(
        mumbleLink(), static_cast<uint32_t>(gw2Pid()), this);
    m_controller->applySettings(m_radialSettings->settings());

    // 4. Wire MumbleLink::dataUpdated → render tick (hotkey polling + rendering)
    connect(mumbleLink(), &MumbleLink::dataUpdated,
            this, &ChildRadial::onRenderTick, Qt::UniqueConnection);

    // 5. Start the controller (hotkey polling only — no rendering until D3D11 init)
    m_controller->startHeadless();

    // D3D11 device, SharedTexture, and IntermediateRT are deferred to
    // ensureD3D11() — called on first map entry (lazy init for B7 fix)
    qInfo() << "[DEV][RADIAL] Init complete (D3D11 DEFERRED):"
            << "controller:" << (m_controller != nullptr)
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

    teardownD3D11();
}

// ============================================================================
// GPU Resource Teardown (Phase 5.5C — full device destruction on unfocus)
// ============================================================================

void ChildRadial::teardownD3D11()
{
    if (!m_d3dInitialized) return;

    qInfo() << "[DEV][RADIAL] Tearing down D3D11 for" << profileName();

    // Invalidate controller's GPU resources (shaders, icon textures, CBs)
    // They were created on this device and will be dangling after destroy.
    // The controller's lazy-init will rebuild them on the next device.
    if (m_controller) {
        m_controller->invalidateGPUResources();
        m_controller->setD3DContext(nullptr);
    }

    // Release intermediate render target
    m_intermediateRTV.Reset();
    m_intermediateRT.Reset();

    // Release shared texture (invalidates compositor consumer)
    if (m_sharedTexture) {
        m_sharedTexture->shutdown();
        delete m_sharedTexture;
        m_sharedTexture = nullptr;
    }

    // Release D3D11 device — frees the device object for other profiles
    if (m_d3dContext) {
        m_d3dContext->shutdown();
        delete m_d3dContext;
        m_d3dContext = nullptr;
    }

    m_d3dInitialized = false;
    qInfo() << "[DEV][RADIAL] D3D11 teardown complete — device freed";
}

// ============================================================================
// Intermediate Render Target
// ============================================================================

bool ChildRadial::createIntermediateRT(int width, int height)
{
    m_intermediateRTV.Reset();
    m_intermediateRT.Reset();

    D3D11_TEXTURE2D_DESC rtDesc = {};
    rtDesc.Width = static_cast<UINT>(width);
    rtDesc.Height = static_cast<UINT>(height);
    rtDesc.MipLevels = 1;
    rtDesc.ArraySize = 1;
    rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Usage = D3D11_USAGE_DEFAULT;
    rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_d3dContext->device()->CreateTexture2D(
        &rtDesc, nullptr, m_intermediateRT.GetAddressOf());
    if (FAILED(hr)) {
        qCritical() << "[DEV][RADIAL] Intermediate RT creation failed:"
                    << Qt::hex << hr;
        return false;
    }

    hr = m_d3dContext->device()->CreateRenderTargetView(
        m_intermediateRT.Get(), nullptr, m_intermediateRTV.GetAddressOf());
    if (FAILED(hr)) {
        qCritical() << "[DEV][RADIAL] Intermediate RTV creation failed:"
                    << Qt::hex << hr;
        m_intermediateRT.Reset();
        return false;
    }

    qInfo() << "[DEV][RADIAL] Intermediate RT created:" << width << "x" << height;
    return true;
}

// ============================================================================
// Render Tick
// ============================================================================

void ChildRadial::onRenderTick()
{
    if (!m_controller || !m_d3dContext || !m_sharedTexture) return;

    // Phase 5.8: Loading screen detection via uiTick stall
    if (mumbleLink() && mumbleLink()->isConnected()) {
        uint32_t currentTick = mumbleLink()->uiTick();
        qint64 now = QDateTime::currentMSecsSinceEpoch();

        if (currentTick != m_lastUiTick) {
            m_lastUiTick = currentTick;
            m_lastTickChangeMs = now;
        }

        bool longStall = (now - m_lastTickChangeMs) >= kStallMs;
        m_controller->setLoadingScreen(longStall);
    }

    // Always poll hotkey (even when unfocused — controller gates internally)
    m_controller->pollHotkey();

    // Only render when wheel is active or fading
    if (!m_controller->needsRendering()) {
        // Phase 5.6: Write one clear frame when rendering stops.
        // The compositor's staging copy retains the last frame — if we don't
        // clear, the radial stays visible forever after hotkey release.
        if (m_lastWroteContent) {
            ID3D11RenderTargetView *rtv = m_sharedTexture->acquireForWrite(0);
            if (rtv) {
                // Clear the intermediate RT to transparent
                float clearColor[4] = {0, 0, 0, 0};
                m_d3dContext->context()->ClearRenderTargetView(
                    m_intermediateRTV.Get(), clearColor);
                // Copy transparent intermediate → shared texture
                m_d3dContext->context()->CopyResource(
                    m_sharedTexture->texture(), m_intermediateRT.Get());
                m_sharedTexture->releaseAfterWrite();
                m_lastWroteContent = false;
            }
        }
        return;
    }

    // Poll GW2 window size
    pollGW2WindowSize();
    if (m_gw2Width <= 0 || m_gw2Height <= 0) return;

    // Resize intermediate RT + shared texture if GW2 window changed
    if (m_sharedTexture->width() != m_gw2Width ||
        m_sharedTexture->height() != m_gw2Height) {
        createIntermediateRT(m_gw2Width, m_gw2Height);
        m_sharedTexture->resize(m_gw2Width, m_gw2Height);
        // Update D3D11Context dimensions so beginFrame viewport is correct
        m_d3dContext->resize(QSize(m_gw2Width, m_gw2Height));
        qInfo() << "[DEV][RADIAL] Resized to" << m_gw2Width << "x" << m_gw2Height;
    }

    // Acquire shared texture for writing (keyed mutex)
    ID3D11RenderTargetView *rtv = m_sharedTexture->acquireForWrite(0);
    if (!rtv) return;  // Mutex contention — skip frame

    // Set intermediate RT as render target (same pattern as Child3DOverlay)
    m_d3dContext->setExternalRTV(m_intermediateRTV.Get());

    // beginFrame: sets blend state + rasterizer + viewport + clear
    m_d3dContext->beginFrame();

    // Get cursor position relative to GW2 window
    POINT cursor = {};
    GetCursorPos(&cursor);
    if (m_gw2Hwnd && IsWindow(m_gw2Hwnd)) {
        ScreenToClient(m_gw2Hwnd, &cursor);
    }

    // Render the wheel to intermediate RT
    m_controller->renderToTarget(m_d3dContext, cursor.x, cursor.y,
                                  m_gw2Width, m_gw2Height);

    // CopyResource: intermediate RT → shared texture
    m_d3dContext->context()->CopyResource(
        m_sharedTexture->texture(), m_intermediateRT.Get());

    // Clear external RTV reference before releasing
    m_d3dContext->setExternalRTV(nullptr);

    // Release shared texture (Flush + ReleaseSync inside)
    m_sharedTexture->releaseAfterWrite();
    m_lastWroteContent = true;
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

    // Lazy D3D11 init — create device on first map entry (B7 fix)
    // Only init if focused — unfocused profiles defer to onFocusChanged(true)
    if (!m_d3dInitialized) {
        if (isFocused()) {
            if (!ensureD3D11()) {
                qWarning() << "[DEV][RADIAL] D3D11 lazy init failed on map entry"
                           << "— will retry on focus gain";
            }
        } else {
            qInfo() << "[DEV][RADIAL] Deferring D3D11 init (unfocused)"
                    << "— will init on focus gain";
        }
    }
}

// ============================================================================
// Lazy D3D11 Initialization (B7 fix)
// ============================================================================

bool ChildRadial::ensureD3D11()
{
    if (m_d3dInitialized) return true;

    qInfo() << "[DEV][RADIAL] Lazy D3D11 init starting for" << profileName();

    // Get current GW2 window dimensions
    findGW2Window();
    int initW = m_gw2Width > 0 ? m_gw2Width : 800;
    int initH = m_gw2Height > 0 ? m_gw2Height : 600;

    // Acquire global device creation mutex (B7 fix — serialize across all children)
    HANDLE hDeviceMutex = CreateMutexW(nullptr, FALSE, L"Global\\GW2AIO_DeviceInit");
    if (hDeviceMutex) {
        qInfo() << "[DEV][RADIAL] Waiting for device creation mutex...";
        WaitForSingleObject(hDeviceMutex, 30000);
    }

    // D3D11 offscreen context
    m_d3dContext = new D3D11Context();
    if (!m_d3dContext->initializeOffscreen(QSize(initW, initH))) {
        qCritical() << "[DEV][RADIAL] D3D11 offscreen init FAILED (E_OUTOFMEMORY?)"
                    << "— will retry on next map entry";
        delete m_d3dContext;
        m_d3dContext = nullptr;
        if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
        return false;
    }
    qInfo() << "[DEV][RADIAL] D3D11 offscreen context created"
            << initW << "x" << initH;

    // Intermediate render target
    if (!createIntermediateRT(initW, initH)) {
        qCritical() << "[DEV][RADIAL] Intermediate RT creation FAILED";
        delete m_d3dContext;
        m_d3dContext = nullptr;
        if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
        return false;
    }

    // SharedTextureProducer
    m_sharedTexture = new SharedTextureProducer();
    QString texName = QStringLiteral("GW2AIO_Tex_%1_radial").arg(profileId());
    if (!m_sharedTexture->initialize(
            m_d3dContext->device(), m_d3dContext->context(),
            initW, initH, texName)) {
        qCritical() << "[DEV][RADIAL] SharedTexture init FAILED";
        delete m_sharedTexture;
        m_sharedTexture = nullptr;
        delete m_d3dContext;
        m_d3dContext = nullptr;
        if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
        return false;
    }
    qInfo() << "[DEV][RADIAL] SharedTexture created:" << texName
            << initW << "x" << initH;

    // Provide D3D11 context to controller for renderer init
    m_controller->setD3DContext(m_d3dContext);

    m_d3dInitialized = true;

    // Release device creation mutex — next child can proceed
    if (hDeviceMutex) {
        ReleaseMutex(hDeviceMutex);
        CloseHandle(hDeviceMutex);
    }

    qInfo() << "[DEV][RADIAL] Lazy D3D11 init COMPLETE:"
            << "sharedTex:" << (m_sharedTexture != nullptr)
            << "intermediateRT:" << (m_intermediateRT.Get() != nullptr);
    return true;
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

    if (focused) {
        // Phase 5.5C: Recreate D3D11 device on focus gain
        if (!m_d3dInitialized && isInGame()) {
            qInfo() << "[DEV][RADIAL] Focus gained — creating D3D11 device";
            if (!ensureD3D11()) {
                qWarning() << "[DEV][RADIAL] D3D11 init failed on focus gain"
                           << "— will retry on next focus gain";
            }
        }
    } else {
        // Phase 5.5C: Full device destruction on unfocus
        teardownD3D11();
    }

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

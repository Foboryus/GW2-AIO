#include "ChildMinimap.h"

#include "core/AppConfig.h"
#include "core/MumbleLink.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerSettingsManager.h"
#include "features/markers/MinimapRenderer.h"
#include "rendering/D3D11Context.h"
#include "rendering/SharedTexture.h"
#include "rendering/TrailPipeline.h"

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
    // MinimapRenderer is a QWidget with no parent — delete explicitly
    delete m_minimapRenderer;
    m_minimapRenderer = nullptr;

    delete m_queryContext;
    m_queryContext = nullptr;

    // GPU resources cleaned up by teardownD3D11()
    teardownD3D11();
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

    // 5. Create MinimapRenderer as offscreen QWidget (no parent, not shown)
    m_minimapRenderer = new MinimapRenderer(
        m_markerManager, mumbleLink(), m_imageCache);
    m_minimapRenderer->setQueryContext(m_queryContext);
    m_minimapRenderer->setAttribute(Qt::WA_DontShowOnScreen);
    qInfo() << "ChildMinimap: MinimapRenderer created (offscreen)";

    // 6. Find GW2 window for dimensions
    findGW2Window();

    // D3D11 device, SharedTexture, and QImage are deferred to
    // ensureD3D11() — called on first map entry (lazy init for B7 fix)

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

    // 8. Wire settings changes
    connect(m_markerSettings, &MarkerSettingsManager::settingsChanged,
            this, &ChildMinimap::syncMinimapSettings);
    syncMinimapSettings();

    // 9. Wire MumbleLink::dataUpdated → render frame
    connect(mumbleLink(), &MumbleLink::dataUpdated,
            this, &ChildMinimap::onRenderFrame, Qt::UniqueConnection);

    qInfo() << "[DEV][MINIMAP] Init complete (D3D11 DEFERRED):"
            << "renderer:" << (m_minimapRenderer != nullptr)
            << "targetPid:" << gw2Pid();

    return true;
}

void ChildMinimap::onShutdown()
{
    qInfo() << "ChildMinimap: Shutting down for" << profileName();

    if (m_minimapRenderer) {
        m_minimapRenderer->stop();
    }

    teardownD3D11();
}

// ============================================================================
// GPU Resource Teardown
// ============================================================================

// Full teardown — destroys ALL GPU resources (shutdown / process exit only)
void ChildMinimap::teardownD3D11()
{
    if (!m_d3dInitialized) return;

    qInfo() << "[DEV][MINIMAP] Full D3D11 teardown for" << profileName();

    // Light teardown first (shared texture + intermediate RT)
    teardownSharedResources();

    // Destroy trail pipeline (holds compiled shaders)
    delete m_trailPipeline;
    m_trailPipeline = nullptr;

    // Release D3D11 context (destructor calls shutdown())
    if (m_d3dContext) {
        delete m_d3dContext;
        m_d3dContext = nullptr;
    }

    m_d3dInitialized = false;
    qInfo() << "[DEV][MINIMAP] Full D3D11 teardown complete — device freed";
}

// Light teardown — only shared texture + intermediate RT (fast, for unfocus)
// D3D11Context and TrailPipeline persist across focus cycles to avoid
// expensive shader recompilation (~150ms per focus gain).
void ChildMinimap::teardownSharedResources()
{
    qInfo() << "[DEV][MINIMAP] Light teardown (shared resources) for" << profileName();

    // Release intermediate RT
    m_intermediateRTV.Reset();
    m_intermediateRT.Reset();

    // Release shared texture
    if (m_sharedTexture) {
        m_sharedTexture->shutdown();
        delete m_sharedTexture;
        m_sharedTexture = nullptr;
    }
}

// ============================================================================
// Rendering
// ============================================================================

void ChildMinimap::onRenderFrame()
{
    if (!m_minimapRenderer || !m_sharedTexture || !m_d3dContext) return;
    if (!m_d3dContext->isInitialized()) return;
    if (!isFocused() || !isInGame()) return;

    // Frame guard: cap at ~30fps (33ms) — matches MinimapRenderer's own guard
    static qint64 s_lastFrameMs = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if ((now - s_lastFrameMs) < 33) return;
    s_lastFrameMs = now;

    // Phase 5.8: Loading screen detection via uiTick stall
    if (mumbleLink() && mumbleLink()->isConnected()) {
        uint32_t currentTick = mumbleLink()->uiTick();

        if (currentTick != m_lastUiTick) {
            m_lastUiTick = currentTick;
            m_lastTickChangeMs = now;
        }

        bool hasValidMap = mumbleLink()->mapId() > 0;
        bool longStall = (now - m_lastTickChangeMs) >= kStallMs;

        bool wasVisible = m_contentVisible;
        m_contentVisible = hasValidMap && !longStall;

        // Drive MinimapRenderer visibility (instant hide/fade-in)
        if (wasVisible && !m_contentVisible) {
            m_minimapRenderer->setShouldBeVisible(false);
            // Clear shared texture to transparent so compositor doesn't
            // show a frozen last frame during loading/character select
            if (m_sharedTexture && m_sharedTexture->isInitialized()) {
                ID3D11RenderTargetView *rtv = m_sharedTexture->acquireForWrite(0);
                if (rtv) {
                    float clearColor[4] = {0, 0, 0, 0};
                    m_d3dContext->context()->ClearRenderTargetView(rtv, clearColor);
                    m_sharedTexture->releaseAfterWrite();
                    qInfo() << "[DEV][MINIMAP] Content hidden — cleared shared texture";
                }
            }
        } else if (!wasVisible && m_contentVisible) {
            m_minimapRenderer->setShouldBeVisible(true);
        }

        // Feed combat state to MinimapRenderer (red border + player indicator)
        m_minimapRenderer->setInCombat(mumbleLink()->isInCombat());
    }

    if (!m_contentVisible) return;

    // Poll GW2 window size (may have changed)
    pollGW2WindowSize();
    if (m_gw2Width <= 0 || m_gw2Height <= 0) return;

    // Resize if needed
    if (m_renderImage.width() != m_gw2Width ||
        m_renderImage.height() != m_gw2Height) {
        m_renderImage = QImage(m_gw2Width, m_gw2Height,
                               QImage::Format_ARGB32_Premultiplied);
        m_sharedTexture->resize(m_gw2Width, m_gw2Height);
        m_d3dContext->resize(QSize(m_gw2Width, m_gw2Height));

        // Recreate intermediate RT at new size
        m_intermediateRTV.Reset();
        m_intermediateRT.Reset();
        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width = static_cast<UINT>(m_gw2Width);
        rtDesc.Height = static_cast<UINT>(m_gw2Height);
        rtDesc.MipLevels = 1;
        rtDesc.ArraySize = 1;
        rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        m_d3dContext->device()->CreateTexture2D(
            &rtDesc, nullptr, m_intermediateRT.GetAddressOf());
        if (m_intermediateRT) {
            m_d3dContext->device()->CreateRenderTargetView(
                m_intermediateRT.Get(), nullptr, m_intermediateRTV.GetAddressOf());
        }

        qInfo() << "[DEV][MINIMAP] Resized to" << m_gw2Width << "x" << m_gw2Height;
    }

    // --- Step 1: Render QPainter minimap markers to QImage ---
    bool hasMarkerContent = m_minimapRenderer->renderToImage(m_renderImage);

    // --- Step 2: Upload QImage to intermediate RT ---
    if (m_intermediateRTV) {
        float clearColor[4] = {0, 0, 0, 0};
        m_d3dContext->context()->ClearRenderTargetView(
            m_intermediateRTV.Get(), clearColor);

        if (hasMarkerContent) {
            // Upload QImage bits via UpdateSubresource
            // QImage::Format_ARGB32_Premultiplied = BGRA = DXGI_FORMAT_B8G8R8A8_UNORM
            D3D11_BOX destBox = {};
            destBox.left = 0;
            destBox.top = 0;
            destBox.front = 0;
            destBox.right = m_gw2Width;
            destBox.bottom = m_gw2Height;
            destBox.back = 1;

            m_d3dContext->context()->UpdateSubresource(
                m_intermediateRT.Get(), 0, &destBox,
                m_renderImage.constBits(),
                static_cast<UINT>(m_renderImage.bytesPerLine()),
                0);
        }
    }

    // --- Step 3: Render GPU minimap/bigmap trails on top of markers ---
    if (m_trailPipeline && m_intermediateRTV) {
        // Sync trail pipeline settings
        bool mainOn = m_markerSettings ? m_markerSettings->renderingEnabled() : true;
        bool showMinimap = mainOn && (m_markerSettings ? m_markerSettings->renderMinimapEnabled() : true);
        bool showBigMap = mainOn && (m_markerSettings ? m_markerSettings->renderBigMapEnabled() : true);

        m_trailPipeline->setShowMinimap(showMinimap);
        m_trailPipeline->setShowBigMap(showBigMap);
        if (m_markerSettings) {
            m_trailPipeline->setMinimapTrailWidth(m_markerSettings->minimapTrailWidth());
            m_trailPipeline->setMinimapOpacity(
                static_cast<float>(m_markerSettings->minimapOpacity()));
        }

        // Set intermediate RT as the render target for trail pipeline
        m_d3dContext->setExternalRTV(m_intermediateRTV.Get());

        // Bind RT + blend state to pipeline (renderMinimap assumes RT is bound)
        ID3D11RenderTargetView *trailRTV = m_intermediateRTV.Get();
        m_d3dContext->context()->OMSetRenderTargets(1, &trailRTV, nullptr);

        // Set alpha blend state for proper trail compositing
        float blendFactor[4] = {0, 0, 0, 0};
        m_d3dContext->context()->OMSetBlendState(
            m_d3dContext->alphaBlendState(), blendFactor, 0xFFFFFFFF);

        // Render minimap trails (composites on top of QPainter markers)
        m_trailPipeline->preloadTextures();
        m_trailPipeline->renderMinimap();
    }

    // --- Step 4: Copy intermediate RT → SharedTexture ---
    ID3D11RenderTargetView *rtv = m_sharedTexture->acquireForWrite(0);
    if (!rtv) return;  // Mutex contention

    if (m_intermediateRT && m_sharedTexture->texture()) {
        m_d3dContext->context()->CopyResource(
            m_sharedTexture->texture(), m_intermediateRT.Get());
    }

    m_sharedTexture->releaseAfterWrite();

    // Dev log: emit frame count every 500 frames
    static uint64_t s_frameCount = 0;
    if (++s_frameCount % 500 == 0) {
        qInfo() << "[DEV][MINIMAP] Frame" << s_frameCount
                << "size:" << m_gw2Width << "x" << m_gw2Height
                << "trails:" << (m_trailPipeline != nullptr);
    }
}

// ============================================================================
// GW2 Window
// ============================================================================

bool ChildMinimap::findGW2Window()
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
        qInfo() << "[DEV][MINIMAP] Found GW2 HWND:" << m_gw2Hwnd
                << "size:" << m_gw2Width << "x" << m_gw2Height;
        return true;
    }
    return false;
}

void ChildMinimap::pollGW2WindowSize()
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
// Map lifecycle
// ============================================================================

void ChildMinimap::onMapEntered(uint32_t mapId)
{
    qInfo() << "ChildMinimap: Map entered:" << mapId << "for" << profileName();

    // Lazy D3D11 init — create device on first map entry (B7 fix)
    // Only init if focused — unfocused profiles defer to onFocusChanged(true)
    if (!m_d3dInitialized) {
        if (isFocused()) {
            if (!ensureD3D11()) {
                qWarning() << "[DEV][MINIMAP] D3D11 lazy init failed on map entry"
                           << "— will retry on focus gain";
            }
        } else {
            qInfo() << "[DEV][MINIMAP] Deferring D3D11 init (unfocused)"
                    << "— will init on focus gain";
        }
    }

    if (!m_packsLoaded) {
        qInfo() << "ChildMinimap: Loading marker packs...";
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        m_packsLoaded = true;
        qInfo() << "ChildMinimap: Packs loaded, count:"
                << m_markerManager->packs().size();
    }

    m_queryContext->mapId = mapId;
    m_markerManager->acquireMap(mapId);
    m_markerManager->setProximityEnabled(true);

    const bool shouldRender = isFocused() && isInGame();
    m_minimapRenderer->setRenderingEnabled(shouldRender);

    qInfo() << "[DIAG] ChildMinimap: MAP_ENTERED"
            << profileName()
            << "mapId:" << mapId
            << "packsLoaded:" << m_packsLoaded
            << "packCount:" << m_markerManager->packs().size()
            << "renderingEnabled:" << shouldRender
            << "d3dReady:" << m_d3dInitialized
            << "focused:" << isFocused();
}

// ============================================================================
// Lazy D3D11 Initialization (B7 fix)
// ============================================================================

bool ChildMinimap::ensureD3D11()
{
    if (m_d3dInitialized && m_sharedTexture) return true;

    findGW2Window();
    int initW = m_gw2Width > 0 ? m_gw2Width : 800;
    int initH = m_gw2Height > 0 ? m_gw2Height : 600;

    // --- Fast path: device + pipeline exist, just recreate shared resources ---
    if (m_d3dInitialized && m_d3dContext && !m_sharedTexture) {
        qInfo() << "[DEV][MINIMAP] Fast D3D11 init (shared resources only) for" << profileName();

        // SharedTextureProducer
        m_sharedTexture = new SharedTextureProducer();
        QString texName = QStringLiteral("GW2AIO_Tex_%1_minimap").arg(profileId());
        if (!m_sharedTexture->initialize(
                m_d3dContext->device(), m_d3dContext->context(),
                initW, initH, texName)) {
            qCritical() << "[DEV][MINIMAP] SharedTexture re-init FAILED";
            delete m_sharedTexture;
            m_sharedTexture = nullptr;
            return false;
        }

        // Intermediate render target
        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width = static_cast<UINT>(initW);
        rtDesc.Height = static_cast<UINT>(initH);
        rtDesc.MipLevels = 1;
        rtDesc.ArraySize = 1;
        rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        m_intermediateRTV.Reset();
        m_intermediateRT.Reset();
        HRESULT hr = m_d3dContext->device()->CreateTexture2D(
            &rtDesc, nullptr, m_intermediateRT.GetAddressOf());
        if (SUCCEEDED(hr)) {
            m_d3dContext->device()->CreateRenderTargetView(
                m_intermediateRT.Get(), nullptr, m_intermediateRTV.GetAddressOf());
        }

        qInfo() << "[DEV][MINIMAP] Fast D3D11 init COMPLETE:"
                << "sharedTex:" << (m_sharedTexture != nullptr)
                << "trailPipeline:" << (m_trailPipeline != nullptr)
                << "size:" << initW << "x" << initH;
        return true;
    }

    // --- Full path: first-time initialization ---
    qInfo() << "[DEV][MINIMAP] Lazy D3D11 init starting for" << profileName();

    // Acquire global device creation mutex (B7 fix — serialize across all children)
    HANDLE hDeviceMutex = CreateMutexW(nullptr, FALSE, L"Global\\GW2AIO_DeviceInit");
    if (hDeviceMutex) {
        qInfo() << "[DEV][MINIMAP] Waiting for device creation mutex...";
        WaitForSingleObject(hDeviceMutex, 30000);
    }

    // D3D11 offscreen context (full — blend states, rasterizer for TrailPipeline)
    m_d3dContext = new D3D11Context();
    if (!m_d3dContext->initializeOffscreen(QSize(initW, initH))) {
        qCritical() << "[DEV][MINIMAP] D3D11 offscreen init FAILED (E_OUTOFMEMORY?)"
                     << "— will retry on next map entry";
        delete m_d3dContext;
        m_d3dContext = nullptr;
        if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
        return false;
    }
    qInfo() << "[DEV][MINIMAP] D3D11 offscreen device created:" << initW << "x" << initH;

    // SharedTextureProducer
    m_sharedTexture = new SharedTextureProducer();
    QString texName = QStringLiteral("GW2AIO_Tex_%1_minimap").arg(profileId());
    if (!m_sharedTexture->initialize(
            m_d3dContext->device(), m_d3dContext->context(),
            initW, initH, texName)) {
        qCritical() << "[DEV][MINIMAP] SharedTexture init FAILED";
        delete m_sharedTexture;
        m_sharedTexture = nullptr;
        delete m_d3dContext;
        m_d3dContext = nullptr;
        if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
        return false;
    }
    qInfo() << "[DEV][MINIMAP] SharedTexture created:" << texName
            << initW << "x" << initH;

    // Intermediate render target (QPainter upload + GPU trail composite)
    {
        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width = static_cast<UINT>(initW);
        rtDesc.Height = static_cast<UINT>(initH);
        rtDesc.MipLevels = 1;
        rtDesc.ArraySize = 1;
        rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_d3dContext->device()->CreateTexture2D(
            &rtDesc, nullptr, m_intermediateRT.GetAddressOf());
        if (FAILED(hr)) {
            qCritical() << "[DEV][MINIMAP] Intermediate RT creation FAILED:" << Qt::hex << hr;
            delete m_sharedTexture; m_sharedTexture = nullptr;
            delete m_d3dContext; m_d3dContext = nullptr;
            if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
            return false;
        }

        hr = m_d3dContext->device()->CreateRenderTargetView(
            m_intermediateRT.Get(), nullptr, m_intermediateRTV.GetAddressOf());
        if (FAILED(hr)) {
            qCritical() << "[DEV][MINIMAP] Intermediate RTV creation FAILED:" << Qt::hex << hr;
            m_intermediateRT.Reset();
            delete m_sharedTexture; m_sharedTexture = nullptr;
            delete m_d3dContext; m_d3dContext = nullptr;
            if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
            return false;
        }
        qInfo() << "[DEV][MINIMAP] Intermediate RT created:" << initW << "x" << initH;
    }

    // TrailPipeline for minimap/bigmap GPU trails (Phase 5.9.1)
    m_trailPipeline = new TrailPipeline(m_d3dContext, mumbleLink(),
                                         m_markerManager, m_markerSettings,
                                         m_imageCache);
    if (!m_trailPipeline->initialize()) {
        qWarning() << "[DEV][MINIMAP] TrailPipeline init failed — markers only";
        delete m_trailPipeline;
        m_trailPipeline = nullptr;
    } else {
        // Propagate query context to trail pipeline
        if (m_queryContext) {
            m_trailPipeline->setQueryContext(m_queryContext);
        }
        qInfo() << "[DEV][MINIMAP] TrailPipeline created for minimap/bigmap trails";
    }

    // Allocate QImage render target
    m_renderImage = QImage(initW, initH, QImage::Format_ARGB32_Premultiplied);
    m_renderImage.fill(Qt::transparent);

    m_d3dInitialized = true;

    // Release device creation mutex — next child can proceed
    if (hDeviceMutex) {
        ReleaseMutex(hDeviceMutex);
        CloseHandle(hDeviceMutex);
    }

    qInfo() << "[DEV][MINIMAP] Lazy D3D11 init COMPLETE:"
            << "sharedTex:" << (m_sharedTexture != nullptr)
            << "trailPipeline:" << (m_trailPipeline != nullptr)
            << "size:" << initW << "x" << initH;
    return true;
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
    const bool shouldRender = focused && isInGame();
    qInfo() << "[DIAG] ChildMinimap: FOCUS_CHANGED"
            << profileName()
            << "focused:" << focused
            << "inGame:" << isInGame()
            << "renderingEnabled:" << shouldRender;

    if (focused) {
        // Recreate shared resources on focus gain
        if (isInGame()) {
            if (!m_d3dInitialized) {
                // First time: full init (device + pipeline + shared resources)
                qInfo() << "[DEV][MINIMAP] Focus gained — creating D3D11 device";
                if (!ensureD3D11()) {
                    qWarning() << "[DEV][MINIMAP] D3D11 init failed on focus gain"
                               << "— will retry on next focus gain";
                }
            } else if (!m_sharedTexture) {
                // Fast path: device + pipeline exist, just recreate shared resources
                qInfo() << "[DEV][MINIMAP] Focus gained — recreating shared resources";
                ensureD3D11();  // Will skip device/pipeline creation, only shared
            }
        }
    } else {
        // Light teardown: only shared texture + intermediate RT
        // Device + TrailPipeline persist (avoid shader recompilation)
        teardownSharedResources();
    }

    if (m_minimapRenderer) {
        m_minimapRenderer->setRenderingEnabled(shouldRender);
    }
}

// ============================================================================
// Settings
// ============================================================================

void ChildMinimap::onSettingsReceived(const QJsonObject &settings)
{
    qInfo() << "ChildMinimap: Settings received";

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

    if (settings.contains("heightFilterEnabled")) {
        m_markerSettings->setHeightFilterEnabled(
            settings["heightFilterEnabled"].toBool());
    }
    if (settings.contains("heightFilterRange")) {
        m_markerSettings->setHeightFilterRange(
            static_cast<float>(settings["heightFilterRange"].toDouble()));
    }
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

    qInfo() << "[DEV][MINIMAP] Settings synced:"
            << "rendering:" << mainOn
            << "minimap:" << m_markerSettings->renderMinimapEnabled()
            << "bigMap:" << m_markerSettings->renderBigMapEnabled()
            << "opacity:" << m_markerSettings->minimapOpacity()
            << "markerScale:" << m_markerSettings->minimapMarkerScale();
}

void ChildMinimap::onReloadPacks()
{
    qInfo() << "ChildMinimap: Reloading pack data from disk";

    if (m_markerSettings) {
        m_markerSettings->loadForProfile(profileId());
    }

    if (m_markerManager) {
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        qInfo() << "ChildMinimap: Packs reloaded, count:"
                << m_markerManager->packs().size();

        if (m_queryContext && m_queryContext->mapId > 0) {
            m_markerManager->acquireMap(m_queryContext->mapId);
        }
    }
}

/**
 * @file Child3DOverlay.cpp
 * @brief 3D overlay child — renders markers/trails to SharedTexture
 *
 * Phase 2 of compositor architecture: renders to offscreen SharedTexture
 * instead of owning a D3D11OverlayWindow. The ChildCompositor composites
 * all layers into its single overlay window.
 */

#include "Child3DOverlay.h"

#include "core/AppConfig.h"
#include "core/MumbleLink.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerSettingsManager.h"
#include "rendering/D3D11Context.h"
#include "rendering/SharedTexture.h"
#include "rendering/ExclusionData.h"

// Pipeline includes (were previously inside D3D11OverlayWindow)
#include "rendering/MarkerPipeline.h"
#include "rendering/TrailPipeline.h"
#include "rendering/SpriteBatch.h"
#include "rendering/GlyphAtlas.h"

#include <QDir>
#include <QDateTime>
#include <QJsonDocument>

// clang-format off
#include <windows.h>
// clang-format on
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
    delete m_queryContext;
    m_queryContext = nullptr;
}

// ============================================================================
// GW2 Window Finding
// ============================================================================

namespace {
struct EnumData { DWORD targetPid; HWND result; };
BOOL CALLBACK enumProc(HWND hwnd, LPARAM lParam) {
    auto *data = reinterpret_cast<EnumData *>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != data->targetPid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, 256);
    QString cls = QString::fromWCharArray(className);
    if (cls == QStringLiteral("ArenaNet_Dx_Window_Class") ||
        cls == QStringLiteral("ArenaNet_Gr_Window_Class")) {
        data->result = hwnd;
        return FALSE; // Found
    }
    return TRUE;
}
} // namespace

HWND Child3DOverlay::findGW2WindowByPid(DWORD pid) {
    EnumData data = {pid, nullptr};
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&data));
    return data.result;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool Child3DOverlay::onInitialize()
{
    qInfo() << "[DEV][3D] === onInitialize ===" << profileName();

    // 1. Create per-child MarkerSettingsManager
    const QString markerStateDir = AppConfig::instance().markerStateDir();
    m_markerSettings = new MarkerSettingsManager(markerStateDir, this);
    m_markerSettings->loadForProfile(profileId());
    qInfo() << "[DEV][3D] MarkerSettings loaded";

    // 2. Create per-child ImageCache
    m_imageCache = new ImageCache(this);

    // 3. Create per-child MarkerManager
    m_markerManager = new MarkerManager(mumbleLink(), this);
    m_markerManager->setMarkerSettings(m_markerSettings);
    m_markerManager->setCacheDir(AppConfig::instance().markerPacksCacheDir());

    // 4. Create per-instance query context
    m_queryContext = new MarkerQueryContext();
    m_queryContext->mapId = 0;
    m_queryContext->settings = m_markerSettings;
    m_queryContext->mumble = mumbleLink();

    // D3D11 device, SharedTexture, pipelines, and IntermediateRT are
    // deferred to ensureD3D11() — called on first map entry (B7 fix).
    // This avoids creating 5 full D3D11 contexts at startup.

    // 5. Connect MumbleLink to render frame
    connect(mumbleLink(), &MumbleLink::dataUpdated,
            this, &Child3DOverlay::onRenderFrame);

    qInfo() << "[DEV][3D] Init complete (D3D11 DEFERRED):"
            << "markerMgr:" << (m_markerManager != nullptr)
            << "targetPid:" << gw2Pid();
    return true;
}

void Child3DOverlay::onShutdown()
{
    qInfo() << "[DEV][3D] Shutting down" << profileName();
    teardownD3D11();
}

// ============================================================================
// GPU Resource Teardown (Phase 5.5C — full device destruction on unfocus)
// ============================================================================

void Child3DOverlay::teardownD3D11()
{
    if (!m_d3dInitialized) return;

    qInfo() << "[DEV][3D] Tearing down D3D11 for" << profileName();

    // Tear down pipelines (GPU-dependent)
    delete m_markerPipeline;
    m_markerPipeline = nullptr;
    delete m_trailPipeline;
    m_trailPipeline = nullptr;
    delete m_spriteBatch;
    m_spriteBatch = nullptr;
    delete m_glyphAtlas;
    m_glyphAtlas = nullptr;

    // Release exclusion CB
    if (m_exclusionCB) {
        m_exclusionCB->Release();
        m_exclusionCB = nullptr;
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
    qInfo() << "[DEV][3D] D3D11 teardown complete — device freed";
}

// ============================================================================
// Map lifecycle
// ============================================================================

void Child3DOverlay::onMapEntered(uint32_t mapId)
{
    qInfo() << "[DEV][3D] Map entered:" << mapId << profileName();

    // Lazy D3D11 init — create device + pipelines on first map entry (B7 fix)
    // Only init if focused — unfocused profiles defer to onFocusChanged(true)
    // to avoid thundering herd (all profiles creating devices simultaneously)
    if (!m_d3dInitialized) {
        if (isFocused()) {
            if (!ensureD3D11()) {
                qWarning() << "[DEV][3D] D3D11 lazy init failed on map entry"
                           << "— will retry on focus gain";
            }
        } else {
            qInfo() << "[DEV][3D] Deferring D3D11 init (unfocused)"
                    << "— will init on focus gain";
        }
    }

    if (!m_packsLoaded) {
        qInfo() << "[DEV][3D] Loading marker packs...";
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        m_packsLoaded = true;
        qInfo() << "[DEV][3D] Packs loaded, count:"
                << m_markerManager->packs().size();
    }

    m_queryContext->mapId = mapId;
    m_markerManager->acquireMap(mapId);
    m_markerManager->setProximityEnabled(true);

    m_renderingEnabled = isFocused() && isInGame();
    m_contentVisible = true;
    m_lastUiTick = 0;
    m_lastTickChangeMs = QDateTime::currentMSecsSinceEpoch();

    qInfo() << "[DEV][3D] MAP_ENTERED rendering:" << m_renderingEnabled
            << "d3dReady:" << m_d3dInitialized;
}

// ============================================================================
// Lazy D3D11 Initialization (B7 fix)
// ============================================================================

bool Child3DOverlay::ensureD3D11()
{
    if (m_d3dInitialized) return true;

    qInfo() << "[DEV][3D] Lazy D3D11 init starting for" << profileName();

    // Find GW2 window for dimensions
    QSize initialSize(1920, 1080); // fallback
    m_gw2Hwnd = findGW2WindowByPid(static_cast<DWORD>(gw2Pid()));
    if (m_gw2Hwnd) {
        RECT gw2Rect = {};
        GetClientRect(m_gw2Hwnd, &gw2Rect);
        int w = gw2Rect.right - gw2Rect.left;
        int h = gw2Rect.bottom - gw2Rect.top;
        if (w > 0 && h > 0) {
            initialSize = QSize(w, h);
        }
        qInfo() << "[DEV][3D] GW2 window found:" << w << "x" << h;
    } else {
        qWarning() << "[DEV][3D] GW2 window not found, using fallback size";
    }

    // Acquire global device creation mutex (B7 fix — serialize across all children)
    HANDLE hDeviceMutex = CreateMutexW(nullptr, FALSE, L"Global\\GW2AIO_DeviceInit");
    if (hDeviceMutex) {
        qInfo() << "[DEV][3D] Waiting for device creation mutex...";
        WaitForSingleObject(hDeviceMutex, 30000); // 30s timeout
    }

    // D3D11 offscreen context (full — blend states, rasterizer, etc.)
    m_d3dContext = new D3D11Context();
    if (!m_d3dContext->initializeOffscreen(initialSize)) {
        qCritical() << "[DEV][3D] D3D11 offscreen init FAILED (E_OUTOFMEMORY?)"
                    << "— will retry on next map entry";
        delete m_d3dContext;
        m_d3dContext = nullptr;
        if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
        return false;
    }
    qInfo() << "[DEV][3D] D3D11 offscreen device created:" << initialSize;

    // SharedTextureProducer
    QString texName = QString("GW2AIO_Tex_%1_3d").arg(profileId());
    m_sharedTexture = new SharedTextureProducer();
    if (!m_sharedTexture->initialize(m_d3dContext->device(),
                                     m_d3dContext->context(),
                                     initialSize.width(), initialSize.height(),
                                     texName)) {
        qCritical() << "[DEV][3D] SharedTextureProducer init FAILED";
        delete m_sharedTexture;
        m_sharedTexture = nullptr;
        delete m_d3dContext;
        m_d3dContext = nullptr;
        if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
        return false;
    }
    qInfo() << "[DEV][3D] SharedTextureProducer created:" << texName;

    // Intermediate render target (non-shared)
    {
        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width = static_cast<UINT>(initialSize.width());
        rtDesc.Height = static_cast<UINT>(initialSize.height());
        rtDesc.MipLevels = 1;
        rtDesc.ArraySize = 1;
        rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_d3dContext->device()->CreateTexture2D(
            &rtDesc, nullptr, m_intermediateRT.GetAddressOf());
        if (FAILED(hr)) {
            qCritical() << "[DEV][3D] Intermediate RT creation FAILED:" << Qt::hex << hr;
            delete m_sharedTexture; m_sharedTexture = nullptr;
            delete m_d3dContext; m_d3dContext = nullptr;
            if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
            return false;
        }

        hr = m_d3dContext->device()->CreateRenderTargetView(
            m_intermediateRT.Get(), nullptr, m_intermediateRTV.GetAddressOf());
        if (FAILED(hr)) {
            qCritical() << "[DEV][3D] Intermediate RTV creation FAILED:" << Qt::hex << hr;
            m_intermediateRT.Reset();
            delete m_sharedTexture; m_sharedTexture = nullptr;
            delete m_d3dContext; m_d3dContext = nullptr;
            if (hDeviceMutex) { ReleaseMutex(hDeviceMutex); CloseHandle(hDeviceMutex); }
            return false;
        }
        qInfo() << "[DEV][3D] Intermediate RT created:" << initialSize;
    }

    // Rendering pipelines
    m_markerPipeline = new MarkerPipeline(m_d3dContext, mumbleLink(),
                                          m_markerManager, m_markerSettings,
                                          m_imageCache);
    if (!m_markerPipeline->initialize()) {
        qWarning() << "[DEV][3D] MarkerPipeline init failed";
        delete m_markerPipeline;
        m_markerPipeline = nullptr;
    }

    m_trailPipeline = new TrailPipeline(m_d3dContext, mumbleLink(),
                                         m_markerManager, m_markerSettings,
                                         m_imageCache);
    if (!m_trailPipeline->initialize()) {
        qWarning() << "[DEV][3D] TrailPipeline init failed";
        delete m_trailPipeline;
        m_trailPipeline = nullptr;
    }

    // Propagate query context to pipelines
    if (m_queryContext) {
        if (m_markerPipeline) m_markerPipeline->setQueryContext(m_queryContext);
        if (m_trailPipeline) m_trailPipeline->setQueryContext(m_queryContext);
    }

    // 2D rendering (distance labels)
    m_spriteBatch = new SpriteBatch(m_d3dContext);
    if (!m_spriteBatch->initialize()) {
        qWarning() << "[DEV][3D] SpriteBatch init failed";
        delete m_spriteBatch;
        m_spriteBatch = nullptr;
    }

    m_glyphAtlas = new GlyphAtlas(m_d3dContext);
    if (!m_glyphAtlas->build("Segoe UI", 12, true)) {
        qWarning() << "[DEV][3D] GlyphAtlas build failed";
        delete m_glyphAtlas;
        m_glyphAtlas = nullptr;
    }

    if (m_markerPipeline && m_spriteBatch && m_glyphAtlas) {
        m_markerPipeline->setSpriteBatch(m_spriteBatch);
        m_markerPipeline->setGlyphAtlas(m_glyphAtlas);
    }

    // Exclusion zone buffer
    createExclusionBuffer();

    m_d3dInitialized = true;

    // Release device creation mutex — next child can proceed
    if (hDeviceMutex) {
        ReleaseMutex(hDeviceMutex);
        CloseHandle(hDeviceMutex);
    }

    qInfo() << "[DEV][3D] Lazy D3D11 init COMPLETE:"
            << "sharedTex:" << (m_sharedTexture != nullptr)
            << "markerPipeline:" << (m_markerPipeline != nullptr)
            << "trailPipeline:" << (m_trailPipeline != nullptr)
            << "spriteBatch:" << (m_spriteBatch != nullptr);
    return true;
}

void Child3DOverlay::onMapLeft()
{
    qInfo() << "[DEV][3D] Map left" << profileName();

    uint32_t oldMapId = m_queryContext->mapId;
    m_queryContext->mapId = 0;

    if (oldMapId > 0) {
        m_markerManager->releaseMap(oldMapId);
    }

    m_markerManager->setProximityEnabled(false);
    m_renderingEnabled = false;
}

// ============================================================================
// Focus
// ============================================================================

void Child3DOverlay::onFocusChanged(bool focused)
{
    m_renderingEnabled = focused && isInGame();

    if (focused) {
        // Phase 5.5C: Recreate D3D11 device on focus gain
        if (!m_d3dInitialized && isInGame()) {
            qInfo() << "[DEV][3D] Focus gained — creating D3D11 device";
            if (!ensureD3D11()) {
                qWarning() << "[DEV][3D] D3D11 init failed on focus gain"
                           << "— will retry on next focus gain";
            }
        }
    } else {
        // Phase 5.5C: Full device destruction on unfocus
        // Frees device object so other profiles can create theirs
        teardownD3D11();
    }

    qInfo() << "[DEV][3D] Focus:" << focused << "rendering:" << m_renderingEnabled;
}

// ============================================================================
// Settings from grandfather (via pipe)
// ============================================================================

void Child3DOverlay::onSettingsReceived(const QJsonObject &settings)
{
    qInfo() << "[DEV][3D] Settings received";

    // --- Rendering toggles ---
    if (settings.contains("renderingEnabled")) {
        m_markerSettings->setRenderingEnabled(
            settings["renderingEnabled"].toBool(true));
    }
    if (settings.contains("render3dEnabled")) {
        m_markerSettings->setRender3dEnabled(
            settings["render3dEnabled"].toBool(true));
    }

    // --- 3D display settings ---
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

    // --- Height filter ---
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
}

void Child3DOverlay::onReloadPacks()
{
    qInfo() << "[DEV][3D] Reloading pack data";

    if (m_markerSettings) {
        m_markerSettings->loadForProfile(profileId());
    }

    if (m_markerManager) {
        m_markerManager->loadPacksFromDirectory(
            AppConfig::instance().markerPacksDir());
        qInfo() << "[DEV][3D] Packs reloaded, count:"
                << m_markerManager->packs().size();

        if (m_queryContext && m_queryContext->mapId > 0) {
            m_markerManager->acquireMap(m_queryContext->mapId);
        }
    }
}

// ============================================================================
// Render Frame (driven by MumbleLink::dataUpdated)
// ============================================================================

void Child3DOverlay::onRenderFrame()
{
    if (!m_d3dContext || !m_d3dContext->isInitialized()) return;
    if (!m_renderingEnabled) return;
    if (!m_sharedTexture || !m_sharedTexture->isInitialized()) return;

    // Loading screen detection (same logic as D3D11OverlayWindow)
    if (mumbleLink() && mumbleLink()->isConnected()) {
        uint32_t currentTick = mumbleLink()->uiTick();
        qint64 now = QDateTime::currentMSecsSinceEpoch();

        if (currentTick != m_lastUiTick) {
            m_lastUiTick = currentTick;
            m_lastTickChangeMs = now;
        }

        bool hasValidMap = mumbleLink()->mapId() > 0;
        bool hasValidPosition = (mumbleLink()->playerX() != 0.0f ||
                                 mumbleLink()->playerY() != 0.0f ||
                                 mumbleLink()->playerZ() != 0.0f);
        // 100ms stall threshold — near-instant loading screen detection.
        // uiTick freezes the moment a loading screen starts, so 100ms is
        // safe (6 frames at 60Hz) while feeling instant to the user.
        bool longStall = (now - m_lastTickChangeMs) >= 100;

        m_contentVisible = hasValidMap && hasValidPosition && !longStall;
    }

    // Dynamic resize: check GW2 window size and resize if changed
    if (m_gw2Hwnd && m_d3dContext && m_sharedTexture) {
        RECT gw2Rect = {};
        GetClientRect(m_gw2Hwnd, &gw2Rect);
        int w = gw2Rect.right - gw2Rect.left;
        int h = gw2Rect.bottom - gw2Rect.top;
        if (w > 0 && h > 0 &&
            (w != m_d3dContext->width() || h != m_d3dContext->height())) {
            qInfo() << "[DEV][3D] GW2 window resized:" << w << "x" << h
                    << "(was" << m_d3dContext->width() << "x" << m_d3dContext->height() << ")";
            m_d3dContext->resize(QSize(w, h));
            m_sharedTexture->resize(w, h);
        }
    }

    // Diagnostic counters
    static uint64_t s_writeSuccess = 0;
    static uint64_t s_writeFail = 0;
    static uint64_t s_contentHidden = 0;

    // Clear shared texture when content becomes hidden (loading screen,
    // character select, or stall). Without this, the compositor keeps
    // showing the last rendered frame as a frozen image.
    if (!m_contentVisible) {
        if (m_lastWroteContent && m_sharedTexture && m_sharedTexture->isInitialized()) {
            ID3D11RenderTargetView *rtv = m_sharedTexture->acquireForWrite(0);
            if (rtv) {
                float clearColor[4] = {0, 0, 0, 0};
                m_d3dContext->context()->ClearRenderTargetView(rtv, clearColor);
                // Copy cleared intermediate → shared texture
                if (m_intermediateRTV) {
                    m_d3dContext->context()->ClearRenderTargetView(
                        m_intermediateRTV.Get(), clearColor);
                    m_d3dContext->context()->CopyResource(
                        m_sharedTexture->texture(), m_intermediateRT.Get());
                }
                m_sharedTexture->releaseAfterWrite();
                m_lastWroteContent = false;
                qInfo() << "[DEV][3D] Content hidden — cleared shared texture";
            }
        }
        ++s_contentHidden;
        return;
    }

    // Acquire shared texture for writing
    ID3D11RenderTargetView *rtv = m_sharedTexture->acquireForWrite(0);
    if (!rtv) {
        ++s_writeFail;
        // Log every 100 failures
        if (s_writeFail % 100 == 1) {
            qInfo() << "[DEV][3D] FrameFlow: writes:" << s_writeSuccess
                    << "acquireFail:" << s_writeFail
                    << "hidden:" << s_contentHidden;
        }
        return;
    }

    ++s_writeSuccess;
    // Log every 50 successful writes
    if (s_writeSuccess % 50 == 1) {
        qInfo() << "[DEV][3D] FrameFlow: writes:" << s_writeSuccess
                << "acquireFail:" << s_writeFail
                << "hidden:" << s_contentHidden;
    }

    // Set the INTERMEDIATE RT as our render target (not the shared texture directly)
    m_d3dContext->setExternalRTV(m_intermediateRTV.Get());

    // Render to intermediate RT
    render();

    // Copy intermediate RT → shared texture
    if (m_sharedTexture && m_sharedTexture->texture()) {
        m_d3dContext->context()->CopyResource(
            m_sharedTexture->texture(), m_intermediateRT.Get());
    }

    // Clear external RTV reference before releasing
    m_d3dContext->setExternalRTV(nullptr);

    // Release shared texture (Flush + ReleaseSync inside)
    m_sharedTexture->releaseAfterWrite();
    m_lastWroteContent = true;
}

void Child3DOverlay::render()
{
    // Pre-load textures before frame
    if (m_markerPipeline) {
        m_markerPipeline->preloadTextures();
    }
    if (m_trailPipeline) {
        m_trailPipeline->preloadTextures();
    }

    // Sync render settings
    if (m_markerSettings) {
        float maxDist = static_cast<float>(m_markerSettings->maxRenderDistance());
        float overlayOpacity = static_cast<float>(m_markerSettings->overlayOpacity());
        float minimapOpacity = static_cast<float>(m_markerSettings->minimapOpacity());

        bool mainOn = m_markerSettings->renderingEnabled();
        bool show3d = mainOn && m_markerSettings->render3dEnabled();
        bool showMinimap = mainOn && m_markerSettings->renderMinimapEnabled();
        bool showBigMap = mainOn && m_markerSettings->renderBigMapEnabled();

        if (m_markerPipeline) {
            m_markerPipeline->setShowMarkers(show3d);
            m_markerPipeline->setMaxRenderDistance(maxDist);
            m_markerPipeline->setShowDistance(m_markerSettings->showDistance());
            m_markerPipeline->setMarkerScale(
                static_cast<float>(m_markerSettings->markerScale()));
            m_markerPipeline->setDistanceLabelOffset(
                static_cast<float>(m_markerSettings->distanceLabelOffset()));
            m_markerPipeline->setOpacity(overlayOpacity);

            int fontSize = m_markerSettings->distanceFontSize();
            if (m_glyphAtlas && m_glyphAtlas->currentFontSize() != fontSize) {
                m_glyphAtlas->build("Segoe UI", fontSize, true);
            }
        }
        if (m_trailPipeline) {
            m_trailPipeline->setShowTrails(show3d);
            m_trailPipeline->setShowMinimap(showMinimap);
            m_trailPipeline->setShowBigMap(showBigMap);
            m_trailPipeline->setMinimapTrailWidth(
                m_markerSettings->minimapTrailWidth());
            m_trailPipeline->setOpacity(overlayOpacity);
            m_trailPipeline->setMinimapOpacity(minimapOpacity);
        }
    }

    m_d3dContext->beginFrame();

    // Exclusion zones
    updateAndBindExclusionZones();

    // 1. Trail meshes (behind markers)
    if (m_trailPipeline) {
        m_trailPipeline->render();
    }

    // 2. Marker billboards (on top of trails)
    if (m_markerPipeline) {
        m_markerPipeline->render();
    }

    // 3. Minimap/bigmap trails
    if (m_trailPipeline) {
        m_trailPipeline->renderMinimap();
    }

    m_d3dContext->endFrame();
}

// ============================================================================
// Exclusion Zone Buffer
// ============================================================================

bool Child3DOverlay::createExclusionBuffer()
{
    if (!m_d3dContext || !m_d3dContext->isInitialized()) return false;

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(ExclusionCB);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_d3dContext->device()->CreateBuffer(
        &cbDesc, nullptr, &m_exclusionCB);

    if (FAILED(hr)) {
        qWarning() << "[DEV][3D] Exclusion CB creation failed";
        return false;
    }
    return true;
}

void Child3DOverlay::updateAndBindExclusionZones()
{
    if (!m_exclusionCB || !m_d3dContext || !m_markerSettings) return;

    ExclusionCB data = {};
    data.fadeEdge = m_markerSettings->exclusionFadeEdge();

    float w = static_cast<float>(m_d3dContext->width());
    float h = static_cast<float>(m_d3dContext->height());

    // Populate zones only if exclusion is enabled and viewport is valid
    int zoneIdx = 0;
    if (m_markerSettings->exclusionEnabled() && w > 0 && h > 0) {
        if (m_markerSettings->minimapZoneEnabled()) {
            data.zones[zoneIdx++] = {(w - 300.0f) / w, (h - 300.0f) / h,
                                     300.0f / w, 300.0f / h};
        }
        if (m_markerSettings->skillBarZoneEnabled()) {
            float barW = 600.0f;
            data.zones[zoneIdx++] = {(w - barW) / (2.0f * w), (h - 80.0f) / h,
                                     barW / w, 80.0f / h};
        }
        if (m_markerSettings->chatZoneEnabled()) {
            data.zones[zoneIdx++] = {0.0f, (h - 200.0f) / h,
                                     400.0f / w, 200.0f / h};
        }
    }
    data.zoneCount = zoneIdx;
    data.screenWidth = w;
    data.screenHeight = h;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_d3dContext->context()->Map(
        m_exclusionCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, &data, sizeof(ExclusionCB));
        m_d3dContext->context()->Unmap(m_exclusionCB, 0);
    }

    // Bind to PS register b2
    m_d3dContext->context()->PSSetConstantBuffers(2, 1, &m_exclusionCB);
}

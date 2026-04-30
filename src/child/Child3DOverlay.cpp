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

    // 5. Create D3D11 device (OFFSCREEN — no window, no swap chain)
    m_d3dContext = new D3D11Context();

    // Find GW2 window to get actual client dimensions (same as old D3D11OverlayWindow)
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

    if (!m_d3dContext->initializeOffscreen(initialSize)) {
        qCritical() << "[DEV][3D] D3D11 offscreen init FAILED";
        return false;
    }
    qInfo() << "[DEV][3D] D3D11 offscreen device created:" << initialSize;

    // 6. Create SharedTextureProducer
    QString texName = QString("GW2AIO_Tex_%1_3d").arg(profileId());
    m_sharedTexture = new SharedTextureProducer();
    if (!m_sharedTexture->initialize(m_d3dContext->device(),
                                     m_d3dContext->context(),
                                     initialSize.width(), initialSize.height(),
                                     texName)) {
        qCritical() << "[DEV][3D] SharedTextureProducer init FAILED";
        return false;
    }
    qInfo() << "[DEV][3D] SharedTextureProducer created:" << texName;

    // 6b. Create intermediate render target (non-shared)
    // D3D11 may not support Draw calls directly to SHARED_KEYEDMUTEX textures.
    // We render to this normal texture, then CopyResource to the shared texture.
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
            qCritical() << "[DEV][3D] Intermediate RT creation failed:" << Qt::hex << hr;
            return false;
        }

        hr = m_d3dContext->device()->CreateRenderTargetView(
            m_intermediateRT.Get(), nullptr, m_intermediateRTV.GetAddressOf());
        if (FAILED(hr)) {
            qCritical() << "[DEV][3D] Intermediate RTV creation failed:" << Qt::hex << hr;
            return false;
        }
        qInfo() << "[DEV][3D] Intermediate RT created:" << initialSize;
    }

    // 7. Initialize rendering pipelines
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

    // 8. Initialize 2D rendering (distance labels)
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

    // 9. Create exclusion zone buffer
    createExclusionBuffer();

    // 10. Connect MumbleLink to render frame
    connect(mumbleLink(), &MumbleLink::dataUpdated,
            this, &Child3DOverlay::onRenderFrame);

    qInfo() << "[DEV][3D] Initialization complete — rendering to SharedTexture";
    return true;
}

void Child3DOverlay::onShutdown()
{
    qInfo() << "[DEV][3D] Shutting down" << profileName();

    // Tear down pipelines
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

    // Release shared texture
    if (m_sharedTexture) {
        m_sharedTexture->shutdown();
        delete m_sharedTexture;
        m_sharedTexture = nullptr;
    }

    // Release D3D11 device
    if (m_d3dContext) {
        m_d3dContext->shutdown();
        delete m_d3dContext;
        m_d3dContext = nullptr;
    }
}

// ============================================================================
// Map lifecycle
// ============================================================================

void Child3DOverlay::onMapEntered(uint32_t mapId)
{
    qInfo() << "[DEV][3D] Map entered:" << mapId << profileName();

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

    qInfo() << "[DEV][3D] MAP_ENTERED rendering:" << m_renderingEnabled;
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
        bool longStall = (now - m_lastTickChangeMs) >= 2000;

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

    if (!m_contentVisible) {
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

    // [TEMP DIAGNOSTIC] Hardcoded triangle — proves DrawIndexed works on SharedTexture RTV
    // If this triangle is visible, the issue is in the pipeline shaders/state
    // If invisible, DrawIndexed fundamentally doesn't work on keyed-mutex textures
    if (!m_diagTriangleVB) {
        // Compile minimal shaders
        static const char *diagVS = R"(
            float4 VS(float2 pos : POSITION) : SV_Position {
                return float4(pos, 0.5, 1.0);
            }
        )";
        static const char *diagPS = R"(
            float4 PS() : SV_Target {
                return float4(1.0, 0.0, 1.0, 0.8); // Bright magenta, premultiplied
            }
        )";
        QString err;
        auto vsBlob = m_d3dContext->compileShader(QByteArray(diagVS), "VS", "vs_5_0", err);
        auto psBlob = m_d3dContext->compileShader(QByteArray(diagPS), "PS", "ps_5_0", err);
        if (vsBlob && psBlob) {
            m_d3dContext->device()->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                m_diagTriangleVS.GetAddressOf());
            m_d3dContext->device()->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                m_diagTrianglePS.GetAddressOf());

            D3D11_INPUT_ELEMENT_DESC layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
            };
            m_d3dContext->device()->CreateInputLayout(
                layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                m_diagTriangleIL.GetAddressOf());

            // Triangle covering top-left quarter in NDC (-1,-1 to 0,0)
            float verts[] = {
                -0.8f, -0.8f,  // top-left
                 0.0f, -0.8f,  // top-right
                -0.8f,  0.0f,  // bottom-left
            };
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = sizeof(verts);
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA sd = {verts};
            m_d3dContext->device()->CreateBuffer(&bd, &sd, m_diagTriangleVB.GetAddressOf());
            qInfo() << "[DEV][3D] Diagnostic triangle created";
        }
    }
    if (m_diagTriangleVB && m_diagTriangleVS && m_diagTrianglePS) {
        auto *ctx = m_d3dContext->context();
        ctx->IASetInputLayout(m_diagTriangleIL.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT stride = sizeof(float) * 2, offset = 0;
        ID3D11Buffer *vbs[] = {m_diagTriangleVB.Get()};
        ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        ctx->VSSetShader(m_diagTriangleVS.Get(), nullptr, 0);
        ctx->PSSetShader(m_diagTrianglePS.Get(), nullptr, 0);
        ctx->Draw(3, 0);
    }

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

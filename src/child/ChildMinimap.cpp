#include "ChildMinimap.h"

#include "core/AppConfig.h"
#include "core/MumbleLink.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerSettingsManager.h"
#include "features/markers/MinimapRenderer.h"
#include "rendering/D3D11Context.h"
#include "rendering/SharedTexture.h"

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

    // 6. D3D11 offscreen context for SharedTexture
    m_d3dContext = new D3D11Context();

    // 7. Find GW2 window for dimensions
    findGW2Window();
    int initW = m_gw2Width > 0 ? m_gw2Width : 800;
    int initH = m_gw2Height > 0 ? m_gw2Height : 600;

    if (!m_d3dContext->initializeOffscreen(QSize(initW, initH))) {
        qCritical() << "ChildMinimap: D3D11 offscreen init failed";
        return false;
    }
    qInfo() << "ChildMinimap: D3D11 offscreen context initialized";

    m_sharedTexture = new SharedTextureProducer();
    QString texName = QStringLiteral("GW2AIO_Tex_%1_minimap").arg(profileId());
    if (!m_sharedTexture->initialize(
            m_d3dContext->device(), m_d3dContext->context(),
            initW, initH, texName)) {
        qCritical() << "ChildMinimap: SharedTexture init failed";
        return false;
    }
    qInfo() << "ChildMinimap: SharedTexture created:" << texName
            << initW << "x" << initH;

    // 9. Allocate QImage render target
    m_renderImage = QImage(initW, initH, QImage::Format_ARGB32_Premultiplied);
    m_renderImage.fill(Qt::transparent);

    // 10. Wire MumbleLink connection → start/stop MinimapRenderer
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

    // 11. Wire settings changes
    connect(m_markerSettings, &MarkerSettingsManager::settingsChanged,
            this, &ChildMinimap::syncMinimapSettings);
    syncMinimapSettings();

    // 12. Wire MumbleLink::dataUpdated → render frame
    connect(mumbleLink(), &MumbleLink::dataUpdated,
            this, &ChildMinimap::onRenderFrame, Qt::UniqueConnection);

    qInfo() << "[DEV][MINIMAP] Init complete:"
            << "renderer:" << (m_minimapRenderer != nullptr)
            << "sharedTex:" << (m_sharedTexture != nullptr)
            << "size:" << initW << "x" << initH
            << "targetPid:" << gw2Pid();

    return true;
}

void ChildMinimap::onShutdown()
{
    qInfo() << "ChildMinimap: Shutting down for" << profileName();

    if (m_minimapRenderer) {
        m_minimapRenderer->stop();
    }

    if (m_sharedTexture) {
        m_sharedTexture->shutdown();
    }
}

// ============================================================================
// Rendering
// ============================================================================

void ChildMinimap::onRenderFrame()
{
    if (!m_minimapRenderer || !m_sharedTexture || !m_d3dContext) return;
    if (!isFocused() || !isInGame()) return;

    // Frame guard: cap at ~30fps (33ms) — matches MinimapRenderer's own guard
    static qint64 s_lastFrameMs = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if ((now - s_lastFrameMs) < 33) return;
    s_lastFrameMs = now;

    // Poll GW2 window size (may have changed)
    pollGW2WindowSize();
    if (m_gw2Width <= 0 || m_gw2Height <= 0) return;

    // Resize if needed
    if (m_renderImage.width() != m_gw2Width ||
        m_renderImage.height() != m_gw2Height) {
        m_renderImage = QImage(m_gw2Width, m_gw2Height,
                               QImage::Format_ARGB32_Premultiplied);
        m_sharedTexture->resize(m_gw2Width, m_gw2Height);
        qInfo() << "[DEV][MINIMAP] Resized to" << m_gw2Width << "x" << m_gw2Height;
    }

    // Render minimap to QImage
    if (!m_minimapRenderer->renderToImage(m_renderImage)) {
        return;  // Nothing to render (not connected, faded out, etc.)
    }

    // Upload QImage → SharedTexture
    ID3D11RenderTargetView *rtv = m_sharedTexture->acquireForWrite(0);
    if (!rtv) return;  // Mutex contention

    // Clear the shared texture to transparent
    float clearColor[4] = {0, 0, 0, 0};
    m_d3dContext->context()->ClearRenderTargetView(rtv, clearColor);

    // Upload QImage bits via UpdateSubresource
    // QImage::Format_ARGB32_Premultiplied = BGRA byte order = DXGI_FORMAT_B8G8R8A8_UNORM
    D3D11_BOX destBox = {};
    destBox.left = 0;
    destBox.top = 0;
    destBox.front = 0;
    destBox.right = m_gw2Width;
    destBox.bottom = m_gw2Height;
    destBox.back = 1;

    m_d3dContext->context()->UpdateSubresource(
        m_sharedTexture->texture(), 0, &destBox,
        m_renderImage.constBits(),
        static_cast<UINT>(m_renderImage.bytesPerLine()),
        0);

    m_sharedTexture->releaseAfterWrite();

    // Dev log: emit frame count every 500 frames
    static uint64_t s_frameCount = 0;
    if (++s_frameCount % 500 == 0) {
        qInfo() << "[DEV][MINIMAP] Frame" << s_frameCount
                << "size:" << m_gw2Width << "x" << m_gw2Height;
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
            << "focused:" << isFocused();
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

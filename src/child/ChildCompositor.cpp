/**
 * @file ChildCompositor.cpp
 * @brief Single compositor window per GW2 instance
 *
 * Owns the ONE overlay window, composites shared texture layers
 * from sibling children via fullscreen quad shader.
 */

#include "ChildCompositor.h"
#include "rendering/D3D11Context.h"
#include "rendering/SharedTexture.h"
#include "core/MumbleLink.h"
#include "core/OverlayZOrder.h"

#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM

#include <QDebug>
#include <QDateTime>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#pragma comment(lib, "dcomp.lib")

// Static hook map for WinEventHook callback routing
static QHash<HWINEVENTHOOK, ChildCompositor *> s_hookMap;

// ============================================================================
// Constructor / Destructor
// ============================================================================

ChildCompositor::ChildCompositor(const QString &profileId,
                                 const QString &mumbleName,
                                 qint64 gw2Pid,
                                 const QString &pipeName,
                                 const QString &profileName,
                                 QObject *parent)
    : ChildProcess(profileId, mumbleName, gw2Pid, pipeName, profileName, parent)
{
  // Layer draw order: bottom to top
  m_layerOrder = {"3d", "map", "radial", "hud"};

  // Unique window class per profile
  std::wstring uuid = profileId.toStdWString();
  m_windowClassName = L"GW2AIO_Compositor_" + uuid;
}

ChildCompositor::~ChildCompositor()
{
  destroyCompositorWindow();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool ChildCompositor::onInitialize()
{
  qInfo() << "[DEV][COMPOSITOR] === onInitialize ==="
          << "profile:" << profileName()
          << "gw2Pid:" << gw2Pid();

  // Find GW2 window by PID
  m_gw2ProcessId = static_cast<uint32_t>(gw2Pid());

  // Create D3D11 context (will be initialized when window is created)
  m_d3dContext = new D3D11Context();

  // Set up render timer (62.5 Hz = 16ms)
  m_renderTimer = new QTimer(this);
  m_renderTimer->setTimerType(Qt::PreciseTimer);
  m_renderTimer->setInterval(16);
  connect(m_renderTimer, &QTimer::timeout, this, &ChildCompositor::onRenderFrame);

  // Try to find GW2 window and create compositor
  // Use MumbleLink dataUpdated to retry if not found yet
  connect(mumbleLink(), &MumbleLink::dataUpdated, this, [this]() {
    if (!m_gw2Hwnd) {
      // Retry finding GW2 window on each MumbleLink tick
      if (findGW2Window()) {
        if (createCompositorWindow()) {
          installEventHook();
          registerProcessExitWait();
          updatePosition();
          m_contentVisible = true;
          m_renderTimer->start();
          // Notify grandfather that compositor is ready
          sendToGrandfather(("COMPOSITOR_READY:" +
                          QString::number(m_d3dContext->width()) + ":" +
                          QString::number(m_d3dContext->height()) + "\n").toUtf8());

          // Create SharedTextureConsumer for each layer (lazy-open later)
          for (const QString &layerKey : m_layerOrder) {
            if (!m_layers.contains(layerKey)) {
              m_layers[layerKey] = new SharedTextureConsumer();
              qInfo() << "[DEV][COMPOSITOR] Created consumer slot for layer:" << layerKey;
            }
          }

          qInfo() << "[DEV][COMPOSITOR] Window created + rendering started"
                  << m_d3dContext->width() << "x" << m_d3dContext->height()
                  << "COMPOSITOR_READY sent"
                  << "consumerSlots:" << m_layers.size();
        }
      }
    }
  });

  return true;
}

void ChildCompositor::onShutdown()
{
  qInfo() << "ChildCompositor: Shutting down for" << profileName();

  if (m_renderTimer) {
    m_renderTimer->stop();
  }

  unregisterProcessExitWait();
  uninstallEventHook();
  destroyCompositorWindow();
}

void ChildCompositor::onMapEntered(uint32_t mapId)
{
  Q_UNUSED(mapId)
  // Compositor doesn't care about map state — it just composites
}

void ChildCompositor::onMapLeft()
{
  // No-op for compositor
}

void ChildCompositor::onFocusChanged(bool focused)
{
  m_gw2Focused = focused;
  m_contentVisible = focused;

  if (m_hwnd) {
    if (focused) {
      // Phase 5.5C: Close all consumers — producers destroyed their devices
      // on the previous unfocus. Consumer handles are stale.
      // tryOpenConsumers() will re-open them when new producers are ready.
      for (auto *consumer : m_layers) {
        if (consumer && consumer->isOpen()) {
          consumer->shutdown();
        }
      }
      qInfo() << "[DEV][COMPOSITOR] Focus gained — closed stale consumers for re-open";

      ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
      updatePosition();

      // Reset stall detection — stale tick timestamps from before unfocus
      // would trigger false stall on first frames after refocus.
      m_lastUiTick = 0;
      m_lastTickChangeMs = 0;
      m_tickStalled = false;

      // Start loading bar only if player is in-game (not loading/char select)
      if (mumbleLink() && mumbleLink()->isConnected() &&
          mumbleLink()->mapId() > 0) {
        m_loadingBarActive = true;
        m_loadingBarStartMs = QDateTime::currentMSecsSinceEpoch();
      }
    } else {
      m_loadingBarActive = false;

      // Clear render target before hiding — prevents DComp from showing
      // the last committed frame (3D markers, etc.) as a brief flash.
      if (m_d3dContext && m_d3dContext->isInitialized()) {
        m_d3dContext->beginFrame();  // Clears RT to transparent
        m_d3dContext->endFrame();    // Present cleared frame
      }

      ShowWindow(m_hwnd, SW_HIDE);
    }
  }

  qInfo() << "ChildCompositor: Focus changed:" << focused;
}

void ChildCompositor::onSettingsReceived(const QJsonObject &settings)
{
  // Handle interactive rects from feature children
  if (settings.contains("interactiveRects")) {
    QString layer = settings["layer"].toString();
    QJsonArray rectsArray = settings["interactiveRects"].toArray();
    QList<QRect> rects;
    for (const QJsonValue &v : rectsArray) {
      QJsonObject r = v.toObject();
      rects.append(QRect(r["x"].toInt(), r["y"].toInt(),
                         r["w"].toInt(), r["h"].toInt()));
    }
    updateInteractiveRects(layer, rects);
  }

  // Handle theme updates (THEME pipe → base class wraps as {"theme": {...}})
  if (settings.contains("theme")) {
    QJsonObject t = settings["theme"].toObject();
    if (t.contains("panelBorder"))
      m_borderColor = QColor(t["panelBorder"].toString());
    if (t.contains("error"))
      m_combatColor = QColor(t["error"].toString());
    if (t.contains("windowBg")) {
      m_loadingBarBg = QColor(t["windowBg"].toString());
      m_loadingBarBg.setAlpha(230);  // Semi-transparent
    }
    if (t.contains("iconColor"))
      m_loadingBarFill = QColor(t["iconColor"].toString());
    if (t.contains("textPrimary")) {
      // Warm-tinted text for loading bar
      QColor base(t["textPrimary"].toString());
      m_loadingBarText = QColor::fromHslF(
          0.1, 0.3, qBound(0.0, base.lightnessF() * 0.95, 1.0));
    }
    qInfo() << "[DEV][COMPOSITOR] Theme updated — border:"
            << m_borderColor.name() << "combat:" << m_combatColor.name();
  }
}

void ChildCompositor::onReloadPacks()
{
  // No-op for compositor
}

void ChildCompositor::onLayerReset(const QString &layerKey)
{
  // Grandfather notifies us that a child for this layer was terminated.
  // Close the stale consumer so tryOpenConsumers() can reopen it when
  // the replacement child spawns and creates a new SharedTexture.
  auto *consumer = m_layers.value(layerKey, nullptr);
  if (consumer && consumer->isOpen()) {
    consumer->shutdown();
    qInfo() << "[DEV][COMPOSITOR] LAYER_RESET: closed stale consumer for"
            << layerKey << "— will reopen on next tryOpenConsumers cycle";
  }
}

// ============================================================================
// GW2 Window Finding
// ============================================================================

namespace {
struct EnumData {
  DWORD targetPid;
  HWND result;
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lParam) {
  auto *data = reinterpret_cast<EnumData *>(lParam);

  DWORD windowPid = 0;
  GetWindowThreadProcessId(hwnd, &windowPid);
  if (data->targetPid != 0 && windowPid != data->targetPid) {
    return TRUE;
  }
  if (!IsWindowVisible(hwnd)) {
    return TRUE;
  }

  wchar_t className[256] = {};
  GetClassNameW(hwnd, className, 256);
  QString windowClass = QString::fromWCharArray(className);
  if (windowClass == QLatin1String("ArenaNet_Dx_Window_Class") ||
      windowClass == QLatin1String("ArenaNet_Gr_Window_Class")) {
    data->result = hwnd;
    return FALSE;
  }
  return TRUE;
}
} // namespace

bool ChildCompositor::findGW2Window()
{
  EnumData data = {};
  data.targetPid = m_gw2ProcessId;
  data.result = nullptr;
  EnumWindows(enumProc, reinterpret_cast<LPARAM>(&data));

  if (data.result) {
    m_gw2Hwnd = data.result;
    qInfo() << "[DEV][COMPOSITOR] Found GW2 window"
            << "PID:" << m_gw2ProcessId
            << "HWND:" << data.result;
    return true;
  }
  return false;
}

// ============================================================================
// Window Creation
// ============================================================================

bool ChildCompositor::createCompositorWindow()
{
  HINSTANCE hInstance = GetModuleHandleW(nullptr);

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = ChildCompositor::windowProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = m_windowClassName.c_str();

  if (!RegisterClassExW(&wc)) {
    DWORD err = GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      qCritical() << "ChildCompositor: RegisterClassEx failed:" << err;
      return false;
    }
  }

  // WS_EX_TRANSPARENT: REQUIRED for click-through with WS_EX_NOREDIRECTIONBITMAP.
  // DComp windows have no per-pixel alpha surface, so without WS_EX_TRANSPARENT
  // Windows treats the entire window as opaque for hit testing — blocking all
  // clicks underneath. WM_NCHITTEST → HTTRANSPARENT is NOT sufficient.
  DWORD exStyle = WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP |
                  WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT;

  std::wstring title = OverlayZOrder::buildTitle(
      OverlayZOrder::kLayerCompositor, L"Compositor");
  m_hwnd = CreateWindowExW(
      exStyle, m_windowClassName.c_str(), title.c_str(),
      WS_POPUP, 0, 0, 100, 100,
      nullptr, nullptr, hInstance, this);

  if (!m_hwnd) {
    qCritical() << "ChildCompositor: CreateWindowEx failed:" << GetLastError();
    return false;
  }

  SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  // Get GW2 window size
  RECT gw2Rect = {};
  if (m_gw2Hwnd) {
    GetClientRect(m_gw2Hwnd, &gw2Rect);
  }
  int width = gw2Rect.right - gw2Rect.left;
  int height = gw2Rect.bottom - gw2Rect.top;
  if (width <= 0) width = 1920;
  if (height <= 0) height = 1080;

  // Initialize D3D11 (serialized via global named mutex — B7 fix)
  HANDLE hDeviceMutex = CreateMutexW(nullptr, FALSE, L"Global\\GW2AIO_DeviceInit");
  if (hDeviceMutex) {
    WaitForSingleObject(hDeviceMutex, 30000); // 30s timeout
  }

  bool d3dOk = m_d3dContext->initialize(m_hwnd, QSize(width, height));

  if (hDeviceMutex) {
    ReleaseMutex(hDeviceMutex);
    CloseHandle(hDeviceMutex);
  }

  if (!d3dOk) {
    qCritical() << "ChildCompositor: D3D11 init failed";
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    return false;
  }

  // DirectComposition
  if (!setupDirectComposition()) {
    qCritical() << "ChildCompositor: DComp setup failed";
    m_d3dContext->shutdown();
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    return false;
  }

  // Initialize quad pipeline for compositing
  if (!initializeQuadPipeline()) {
    qWarning() << "ChildCompositor: Quad pipeline init failed";
    // Non-fatal — window still works, just can't composite yet
  }

  ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
  qInfo() << "[DEV][COMPOSITOR] Window created"
          << width << "x" << height
          << "HWND:" << m_hwnd
          << "D3D11:OK DComp:OK Shader:" << (m_quadVS ? "OK" : "FAIL");
  return true;
}

void ChildCompositor::destroyCompositorWindow()
{
  if (m_renderTimer) {
    m_renderTimer->stop();
  }

  // Release shared texture consumers
  qDeleteAll(m_layers);
  m_layers.clear();

  // Release quad pipeline
  m_quadVS.Reset();
  m_quadPS.Reset();
  m_quadVB.Reset();
  m_quadLayout.Reset();
  m_linearSampler.Reset();
  m_screenSizeCB.Reset();

  // Release DComp
  m_dcompVisual.Reset();
  m_dcompTarget.Reset();
  m_dcompDevice.Reset();

  // Release D3D11
  if (m_d3dContext) {
    m_d3dContext->shutdown();
    delete m_d3dContext;
    m_d3dContext = nullptr;
  }

  if (m_hwnd) {
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
  }
}

bool ChildCompositor::setupDirectComposition()
{
  ComPtr<IDXGIDevice> dxgiDevice;
  HRESULT hr = m_d3dContext->device()->QueryInterface(
      IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
  if (FAILED(hr)) return false;

  hr = DCompositionCreateDevice(dxgiDevice.Get(),
                                IID_PPV_ARGS(m_dcompDevice.GetAddressOf()));
  if (FAILED(hr)) return false;

  hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, true,
                                          m_dcompTarget.GetAddressOf());
  if (FAILED(hr)) return false;

  hr = m_dcompDevice->CreateVisual(m_dcompVisual.GetAddressOf());
  if (FAILED(hr)) return false;

  hr = m_dcompVisual->SetContent(m_d3dContext->swapChain());
  if (FAILED(hr)) return false;

  hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
  if (FAILED(hr)) return false;

  hr = m_dcompDevice->Commit();
  if (FAILED(hr)) return false;

  qInfo() << "ChildCompositor: DirectComposition setup complete";
  return true;
}

// ============================================================================
// Fullscreen Quad Pipeline
// ============================================================================

bool ChildCompositor::initializeQuadPipeline()
{
  if (!m_d3dContext || !m_d3dContext->device()) return false;

  // Vertex shader: generate fullscreen triangle from vertex ID
  static const char *vsSource = R"(
    struct VSOut {
      float4 pos : SV_Position;
      float2 uv  : TEXCOORD0;
    };
    VSOut VS(uint id : SV_VertexID) {
      VSOut o;
      o.uv = float2((id << 1) & 2, id & 2);
      o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
      return o;
    }
  )";

  // Pixel shader: [FIX-6] Real passthrough compositor
  // DIAG #14 confirmed: SV_Position UV bypass works, 3D content visible.
  // Now just pass through the shared texture content with premultiplied alpha.
  // Blend state (SRC=ONE, DEST=INV_SRC_ALPHA) in beginFrame() handles compositing.
  static const char *psSource = R"(
    Texture2D layerTex : register(t0);
    SamplerState samp  : register(s0);
    cbuffer ScreenSize : register(b0) {
      float2 screenSize;
      float2 pad;
    };
    float4 PS(float4 pos : SV_Position) : SV_Target {
      float2 uv = pos.xy / screenSize;
      return layerTex.Sample(samp, uv);
    }
  )";

  QString err;
  auto vsBlob = m_d3dContext->compileShader(
      QByteArray(vsSource), "VS", "vs_5_0", err);
  if (!vsBlob) {
    qCritical() << "ChildCompositor: VS compile failed:" << err;
    return false;
  }

  auto psBlob = m_d3dContext->compileShader(
      QByteArray(psSource), "PS", "ps_5_0", err);
  if (!psBlob) {
    qCritical() << "ChildCompositor: PS compile failed:" << err;
    return false;
  }

  auto *dev = m_d3dContext->device();

  HRESULT hr = dev->CreateVertexShader(
      vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
      nullptr, m_quadVS.GetAddressOf());
  if (FAILED(hr)) return false;

  hr = dev->CreatePixelShader(
      psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
      nullptr, m_quadPS.GetAddressOf());
  if (FAILED(hr)) return false;

  // Linear sampler for texture filtering
  m_linearSampler = m_d3dContext->createLinearSampler();

  // Constant buffer for screen size (16 bytes: float2 screenSize + float2 pad)
  D3D11_BUFFER_DESC cbDesc = {};
  cbDesc.ByteWidth = 16;
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = dev->CreateBuffer(&cbDesc, nullptr, m_screenSizeCB.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "ChildCompositor: screenSize CB creation failed";
    return false;
  }

  qInfo() << "ChildCompositor: Quad pipeline initialized (SV_Position UV bypass)";
  return true;
}

// ============================================================================
// Rendering
// ============================================================================

void ChildCompositor::onRenderFrame()
{
  if (!m_hwnd || !m_d3dContext || !m_d3dContext->isInitialized()) return;
  if (!m_contentVisible) return;

  // --- uiTick stall detection (B24/B25: instant loading screen hide) ---
  if (mumbleLink() && mumbleLink()->isConnected()) {
    uint32_t currentTick = mumbleLink()->uiTick();
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (currentTick != m_lastUiTick) {
      m_lastUiTick = currentTick;
      m_lastTickChangeMs = now;
    }

    // 100ms stall threshold — matches Child3DOverlay for consistency
    m_tickStalled = (m_lastTickChangeMs > 0 &&
                     (now - m_lastTickChangeMs) >= 100);
  }

  // Dev log: emit frame count every 1000 frames (~16s at 62.5Hz)
  static uint64_t s_frameCount = 0;
  s_frameCount++;
  if (s_frameCount % 1000 == 0) {
    int openCount = 0;
    for (auto *c : m_layers) { if (c && c->isOpen()) ++openCount; }
    qInfo() << "[DEV][COMPOSITOR] Frame" << s_frameCount
            << "layers:" << m_layers.size()
            << "open:" << openCount
            << "interactiveRects:" << m_interactiveRects.size()
            << "stalled:" << m_tickStalled;
  }

  // Lazy-open: try to open consumers that haven't connected yet.
  // Producers (feature children) may not be ready at compositor start.
  // Retry every ~2s (125 frames at 62.5Hz) to avoid hammering.
  if (s_frameCount % 125 == 1) {
    tryOpenConsumers();
  }

  m_d3dContext->beginFrame();

  // When stalled (loading/char-select), show only the loading bar —
  // skip feature layers and decorations for instant hide.
  if (!m_tickStalled) {
    // Composite shared texture layers from feature children
    renderLayers();

    // Decorations: border + player location icon (on top of layers)
    renderDecorations();
  }

  // Loading bar overlay (renders on top of everything)
  if (m_loadingBarActive) {
    renderLoadingBar();
  }

  m_d3dContext->endFrame();
}

void ChildCompositor::renderLayers()
{
  auto *ctx = m_d3dContext->context();
  if (!ctx || !m_quadVS || !m_quadPS) return;

  // Set up quad pipeline state
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->IASetInputLayout(nullptr);  // No input layout needed (SV_VertexID)
  ctx->VSSetShader(m_quadVS.Get(), nullptr, 0);
  ctx->PSSetShader(m_quadPS.Get(), nullptr, 0);

  ID3D11SamplerState *samplers[] = {m_linearSampler.Get()};
  ctx->PSSetSamplers(0, 1, samplers);

  // Update screen size constant buffer (PS b0)
  if (m_screenSizeCB) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(m_screenSizeCB.Get(), 0,
                          D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
      float cbData[4] = {
        static_cast<float>(m_d3dContext->width()),
        static_cast<float>(m_d3dContext->height()),
        0.0f, 0.0f  // pad
      };
      memcpy(mapped.pData, cbData, sizeof(cbData));
      ctx->Unmap(m_screenSizeCB.Get(), 0);
    }
    ID3D11Buffer *cbs[] = {m_screenSizeCB.Get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
  }

  // Diagnostic counters (static — survive across calls)
  static uint64_t s_acquireSuccess = 0;
  static uint64_t s_acquireFail = 0;
  static uint64_t s_notOpen = 0;

  // Draw each layer in order (bottom to top)
  for (const QString &layerKey : m_layerOrder) {
    auto *consumer = m_layers.value(layerKey, nullptr);
    if (!consumer || !consumer->isOpen()) {
      ++s_notOpen;
      continue;
    }

    // acquireForRead: mutex → CopyResource(staging) → release mutex → return staging SRV
    // Mutex is NOT held during Draw — zero contention with producer
    ID3D11ShaderResourceView *srv = consumer->acquireForRead(0);
    if (!srv) {
      ++s_acquireFail;
      continue;  // Producer busy or idle — skip this frame
    }

    ++s_acquireSuccess;
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->Draw(3, 0);  // Fullscreen triangle
  }

  // Log frame flow every ~4s (250 frames at 62.5Hz)
  if ((s_acquireSuccess + s_acquireFail) > 0 &&
      (s_acquireSuccess + s_acquireFail + s_notOpen) % 250 == 0) {
    qInfo() << "[DEV][COMPOSITOR] FrameFlow:"
            << "acquired:" << s_acquireSuccess
            << "failed:" << s_acquireFail
            << "notOpen:" << s_notOpen;
  }
}

void ChildCompositor::tryOpenConsumers()
{
  if (!m_d3dContext || !m_d3dContext->device()) return;

  // QI to ID3D11Device1 (required by OpenSharedResourceByName)
  ComPtr<ID3D11Device1> device1;
  HRESULT hr = m_d3dContext->device()->QueryInterface(
      IID_PPV_ARGS(device1.GetAddressOf()));
  if (FAILED(hr)) {
    qWarning() << "[DEV][COMPOSITOR] ID3D11Device1 QI failed — cannot open shared textures";
    return;
  }

  for (const QString &layerKey : m_layerOrder) {
    auto *consumer = m_layers.value(layerKey, nullptr);
    if (!consumer || consumer->isOpen()) continue;

    // Build the shared texture name: GW2AIO_Tex_<profileId>_<layerKey>
    QString texName = QString("GW2AIO_Tex_%1_%2").arg(profileId(), layerKey);

    if (consumer->open(device1.Get(), texName)) {
      qInfo() << "[DEV][COMPOSITOR] Opened shared texture:" << texName
              << "layer:" << layerKey;
    }
    // Failure is expected — producer may not exist yet. Will retry.
  }
}

// ============================================================================
// Loading Bar
// ============================================================================

bool ChildCompositor::ensureLoadingBarTexture()
{
  if (!m_d3dContext || !m_d3dContext->device()) return false;

  int screenW = m_d3dContext->width();
  int screenH = m_d3dContext->height();

  // Recreate if screen size changed
  if (m_loadingBarTex) {
    D3D11_TEXTURE2D_DESC existing = {};
    m_loadingBarTex->GetDesc(&existing);
    if (static_cast<int>(existing.Width) == screenW &&
        static_cast<int>(existing.Height) == screenH) {
      return true;  // Same size — reuse
    }
    // Screen resized — recreate
    m_loadingBarSRV.Reset();
    m_loadingBarTex.Reset();
  }

  // Create a fullscreen texture (same size as compositor render target).
  // The existing PS shader uses pos.xy/screenSize for UV sampling, so
  // the texture must match the screen dimensions exactly.
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(screenW);
  desc.Height = static_cast<UINT>(screenH);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // QImage ARGB32_Premultiplied
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

  HRESULT hr = m_d3dContext->device()->CreateTexture2D(
      &desc, nullptr, m_loadingBarTex.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "[DEV][COMPOSITOR] Loading bar texture creation failed:" << hr;
    return false;
  }

  hr = m_d3dContext->device()->CreateShaderResourceView(
      m_loadingBarTex.Get(), nullptr, m_loadingBarSRV.GetAddressOf());
  if (FAILED(hr)) {
    m_loadingBarTex.Reset();
    return false;
  }

  // Premultiplied alpha blend state for the loading bar quad
  if (!m_premulBlend) {
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_d3dContext->device()->CreateBlendState(
        &blendDesc, m_premulBlend.GetAddressOf());
  }

  // Pre-allocate QImage at bar size (NOT fullscreen — saves ~8MB per frame)
  // We upload only the bar region via D3D11_BOX targeting the centered position.
  m_loadingBarImage = QImage(kBarWidth, kBarHeight,
                             QImage::Format_ARGB32_Premultiplied);

  // Clear the fullscreen texture to transparent once (avoids per-frame clear).
  // Need a temporary RTV for ClearRenderTargetView.
  ComPtr<ID3D11RenderTargetView> tempRTV;
  hr = m_d3dContext->device()->CreateRenderTargetView(
      m_loadingBarTex.Get(), nullptr, tempRTV.GetAddressOf());
  if (SUCCEEDED(hr) && m_d3dContext->context()) {
    float clear[4] = {0, 0, 0, 0};
    m_d3dContext->context()->ClearRenderTargetView(tempRTV.Get(), clear);
  }

  qInfo() << "[DEV][COMPOSITOR] Loading bar texture created"
          << screenW << "x" << screenH;
  return true;
}

void ChildCompositor::updateLoadingBarImage(float progress)
{
  m_loadingBarImage.fill(Qt::transparent);
  QPainter p(&m_loadingBarImage);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  // --- Theme colors (from THEME IPC, defaults to Classic Gold) ---
  const QColor bgColor = m_loadingBarBg;
  const QColor borderColor = m_borderColor;          // Gold border
  const QColor barBg(0x3A, 0x3A, 0x3A);              // Progress track (neutral)
  const QColor barFill = m_loadingBarFill;             // Gold fill
  const QColor textColor = m_loadingBarText;           // Warm white

  const int pad = 4;
  const QRectF outer(0.5, 0.5, kBarWidth - 1, kBarHeight - 1);
  const int cornerR = 8;

  // Background + border
  p.setPen(QPen(borderColor, 1.5));
  p.setBrush(bgColor);
  p.drawRoundedRect(outer, cornerR, cornerR);

  // AIO icon (left side)
  const int iconSize = 32;
  const int iconX = pad + 6;
  const int iconY = (kBarHeight - iconSize) / 2;
  {
    QSvgRenderer svg(QString(":/icons/app-icon.svg"));
    if (svg.isValid()) {
      svg.render(&p, QRectF(iconX, iconY, iconSize, iconSize));
    }
  }

  // "Loading AIO" text
  const int textX = iconX + iconSize + 8;
  const int textY = pad + 2;
  const int textH = 20;
  QFont font("Segoe UI", 10, QFont::DemiBold);
  p.setFont(font);
  p.setPen(textColor);
  p.drawText(QRectF(textX, textY, kBarWidth - textX - pad, textH),
             Qt::AlignLeft | Qt::AlignVCenter, "Loading AIO");

  // Progress bar track
  const int barX = textX;
  const int barY = textY + textH + 4;
  const int barW = kBarWidth - barX - pad - 8;
  const int barH = 10;
  const int barR = 5;

  p.setPen(Qt::NoPen);
  p.setBrush(barBg);
  p.drawRoundedRect(QRectF(barX, barY, barW, barH), barR, barR);

  // Progress fill
  int fillW = static_cast<int>(barW * qBound(0.0f, progress, 1.0f));
  if (fillW > 0) {
    QPainterPath clip;
    clip.addRoundedRect(QRectF(barX, barY, barW, barH), barR, barR);
    p.setClipPath(clip);
    p.setBrush(barFill);
    p.drawRect(QRectF(barX, barY, fillW, barH));
    p.setClipping(false);
  }

  p.end();
}

void ChildCompositor::renderLoadingBar()
{
  if (!ensureLoadingBarTexture()) return;

  auto *ctx = m_d3dContext->context();
  if (!ctx) return;

  // Calculate progress (0.0 → 1.0 over kBarDurationMs)
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  qint64 elapsed = now - m_loadingBarStartMs;
  float progress = static_cast<float>(elapsed) / static_cast<float>(kBarDurationMs);

  if (progress >= 1.0f) {
    m_loadingBarActive = false;
    return;
  }

  // Render bar to bar-sized QImage (320×56)
  updateLoadingBarImage(progress);

  // Upload bar QImage into the top-center region of the fullscreen texture.
  int screenW = m_d3dContext->width();
  int destX = (screenW - kBarWidth) / 2;
  int destY = 25;  // 25px from top border

  D3D11_BOX box = {};
  box.left = static_cast<UINT>(destX);
  box.top = static_cast<UINT>(destY);
  box.front = 0;
  box.right = static_cast<UINT>(destX + kBarWidth);
  box.bottom = static_cast<UINT>(destY + kBarHeight);
  box.back = 1;
  ctx->UpdateSubresource(
      m_loadingBarTex.Get(), 0, &box,
      m_loadingBarImage.constBits(),
      static_cast<UINT>(m_loadingBarImage.bytesPerLine()),
      0);

  // Enable premultiplied alpha blending
  float blendFactor[4] = {0, 0, 0, 0};
  ctx->OMSetBlendState(m_premulBlend.Get(), blendFactor, 0xFFFFFFFF);

  // Reuse the existing fullscreen-triangle pipeline (SV_VertexID + SV_Position UV).
  // The bar texture is screen-sized with the bar centered — transparent pixels
  // outside the bar area composite as no-op.
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->IASetInputLayout(nullptr);
  ctx->VSSetShader(m_quadVS.Get(), nullptr, 0);
  ctx->PSSetShader(m_quadPS.Get(), nullptr, 0);

  // screenSize CB (already set by renderLayers, but update in case)
  if (m_screenSizeCB) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx->Map(m_screenSizeCB.Get(), 0,
                           D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      float cbData[4] = {
        static_cast<float>(m_d3dContext->width()),
        static_cast<float>(m_d3dContext->height()),
        0.0f, 0.0f
      };
      memcpy(mapped.pData, cbData, sizeof(cbData));
      ctx->Unmap(m_screenSizeCB.Get(), 0);
    }
    ID3D11Buffer *cbs[] = {m_screenSizeCB.Get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
  }

  ID3D11SamplerState *samplers[] = {m_linearSampler.Get()};
  ctx->PSSetSamplers(0, 1, samplers);

  ctx->PSSetShaderResources(0, 1, m_loadingBarSRV.GetAddressOf());
  ctx->Draw(3, 0);

  // Restore default blend state
  ctx->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

// ============================================================================
// Decorations — Border + Player Location Icon
// ============================================================================

bool ChildCompositor::ensureDecorTexture()
{
  if (!m_d3dContext || !m_d3dContext->device()) return false;

  int screenW = m_d3dContext->width();
  int screenH = m_d3dContext->height();

  // Recreate if screen size changed
  if (m_decorTex) {
    D3D11_TEXTURE2D_DESC existing = {};
    m_decorTex->GetDesc(&existing);
    if (static_cast<int>(existing.Width) == screenW &&
        static_cast<int>(existing.Height) == screenH) {
      return true;  // Same size — reuse
    }
    m_decorSRV.Reset();
    m_decorTex.Reset();
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(screenW);
  desc.Height = static_cast<UINT>(screenH);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

  HRESULT hr = m_d3dContext->device()->CreateTexture2D(
      &desc, nullptr, m_decorTex.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "[DEV][COMPOSITOR] Decoration texture creation failed:" << hr;
    return false;
  }

  hr = m_d3dContext->device()->CreateShaderResourceView(
      m_decorTex.Get(), nullptr, m_decorSRV.GetAddressOf());
  if (FAILED(hr)) {
    m_decorTex.Reset();
    return false;
  }

  // Clear to transparent
  ComPtr<ID3D11RenderTargetView> tempRTV;
  hr = m_d3dContext->device()->CreateRenderTargetView(
      m_decorTex.Get(), nullptr, tempRTV.GetAddressOf());
  if (SUCCEEDED(hr) && m_d3dContext->context()) {
    float clear[4] = {0, 0, 0, 0};
    m_d3dContext->context()->ClearRenderTargetView(tempRTV.Get(), clear);
  }

  qInfo() << "[DEV][COMPOSITOR] Decoration texture created"
          << screenW << "x" << screenH;
  return true;
}

QRectF ChildCompositor::computeMinimapRect(int screenW, int screenH) const
{
  if (!mumbleLink() || !mumbleLink()->isConnected()) {
    return QRectF();
  }

  const CompassData &compass = mumbleLink()->minimapData();
  if (compass.compassWidth <= 0 || compass.compassHeight <= 0) {
    return QRectF();
  }

  // GW2 window-too-small scaling (TacO: GetWindowTooSmallScale)
  constexpr float kMinWindowWidth = 1024.0f;
  constexpr float kMinWindowHeight = 768.0f;
  float wtsW = (screenW < kMinWindowWidth)
                   ? static_cast<float>(screenW) / kMinWindowWidth
                   : 1.0f;
  float wtsH = (screenH < kMinWindowHeight)
                   ? static_cast<float>(screenH) / kMinWindowHeight
                   : 1.0f;
  float windowTooSmallScale = qMin(wtsW, wtsH);

  float cw = static_cast<float>(compass.compassWidth) * windowTooSmallScale;
  float ch = static_cast<float>(compass.compassHeight) * windowTooSmallScale;

  float x, y;
  if (mumbleLink()->isMinimapTopRight()) {
    x = static_cast<float>(screenW) - cw;
    y = 1.0f * windowTooSmallScale;
  } else {
    // Default: bottom-right with delta for bottom UI bar
    int uiSize = mumbleLink()->uiSize();
    float delta = 37.0f; // Normal UI
    if (uiSize == 0)
      delta = 33.0f; // Small
    else if (uiSize == 2)
      delta = 41.0f; // Large
    else if (uiSize == 3)
      delta = 45.0f; // Larger

    delta *= windowTooSmallScale;

    x = static_cast<float>(screenW) - cw;
    y = static_cast<float>(screenH) - ch - delta;
  }

  return QRectF(x, y, cw, ch);
}

void ChildCompositor::paintDecorations(QPainter &p, int screenW, int screenH)
{
  if (!mumbleLink() || !mumbleLink()->isConnected()) return;
  if (mumbleLink()->mapId() == 0) return;

  bool bigMapOpen = mumbleLink()->isMapOpen();
  bool inCombat = mumbleLink()->isInCombat();

  // Compute minimap rect (always needed for minimap border)
  QRectF miniRect = computeMinimapRect(screenW, screenH);

  // Render rect: big map = fullscreen, minimap = compass rect
  QRectF renderRect = bigMapOpen
      ? QRectF(0, 0, screenW, screenH)
      : miniRect;

  if (renderRect.isEmpty()) return;

  // Choose compass data based on map mode
  const CompassData &compass =
      bigMapOpen ? mumbleLink()->bigMapData() : mumbleLink()->minimapData();

  // Build transformation matrix (world → screen pixel coords)
  bool ignoreRotation = bigMapOpen;
  QMatrix4x4 transform = compass.buildTransformationMatrix(
      renderRect, mumbleLink()->playerPosition(), ignoreRotation);

  // --- Player position indicator ---
  {
    QVector3D playerPos = mumbleLink()->playerPosition();
    QVector4D playerWorld(playerPos.x(), playerPos.y(), playerPos.z(), 1.0f);
    QVector4D playerScreen = transform * playerWorld;
    QPointF playerPt(static_cast<qreal>(playerScreen.x()),
                     static_cast<qreal>(playerScreen.y()));

    qreal iconRadius = bigMapOpen ? 10.0 : 7.0;
    qreal borderRadius = iconRadius + 3.0;

    // Facing direction
    QVector3D front = mumbleLink()->playerFront();
    qreal targetAngle = std::atan2(static_cast<qreal>(front.x()),
                                   static_cast<qreal>(front.z()));

    // On minimap, compass rotates — subtract compass rotation
    if (!bigMapOpen) {
      targetAngle -= static_cast<qreal>(compass.compassRotation);
    }

    m_smoothedFacingAngle = targetAngle;
    qreal facingAngle = m_smoothedFacingAngle;

    // Theme color for indicator
    QColor indicatorColor;
    if (inCombat) {
      indicatorColor = m_combatColor;
    } else {
      // Lighten border color: keep hue, reduce saturation, high lightness
      indicatorColor = QColor::fromHslF(
          m_borderColor.hslHueF(),
          m_borderColor.hslSaturationF() * 0.5,
          0.80);
    }

    // Clip to render rect
    p.save();
    p.setClipRect(renderRect);
    p.setOpacity(1.0);

    // --- Directional arrow (behind disc) ---
    {
      qreal arrowLen = bigMapOpen ? 10.0 : 8.0;
      qreal arrowHalfWidth = bigMapOpen ? 9.0 : 7.0;
      qreal baseDist = borderRadius - 3.0;
      qreal tipDist = borderRadius + arrowLen;

      QPointF tip(playerPt.x() + tipDist * std::sin(facingAngle),
                  playerPt.y() - tipDist * std::cos(facingAngle));

      QPointF base1(
          playerPt.x() +
              baseDist * std::sin(facingAngle) -
              arrowHalfWidth * std::cos(facingAngle),
          playerPt.y() -
              baseDist * std::cos(facingAngle) -
              arrowHalfWidth * std::sin(facingAngle));
      QPointF base2(
          playerPt.x() +
              baseDist * std::sin(facingAngle) +
              arrowHalfWidth * std::cos(facingAngle),
          playerPt.y() -
              baseDist * std::cos(facingAngle) +
              arrowHalfWidth * std::sin(facingAngle));

      QPainterPath arrowPath;
      arrowPath.moveTo(tip);
      arrowPath.lineTo(base1);
      arrowPath.lineTo(base2);
      arrowPath.closeSubpath();

      p.setBrush(indicatorColor);
      p.setPen(QPen(Qt::black, 1.5));
      p.drawPath(arrowPath);
    }

    // --- Solid black disc ---
    p.setBrush(QColor(0, 0, 0));
    p.setPen(Qt::NoPen);
    p.drawEllipse(playerPt, borderRadius, borderRadius);

    // --- AIO gear icon ---
    {
      if (m_playerIcon.isNull()) {
        m_playerIcon =
            QPixmap(QStringLiteral(":/icons/player-indicator.svg"));
      }

      if (!m_playerIcon.isNull()) {
        qreal iconSize = iconRadius * 2.0;
        QRectF iconRect(playerPt.x() - iconRadius,
                        playerPt.y() - iconRadius, iconSize, iconSize);
        p.drawPixmap(iconRect.toRect(), m_playerIcon);
      } else {
        // Fallback: black dot
        p.setBrush(QColor(0, 0, 0));
        p.setPen(Qt::NoPen);
        p.drawEllipse(playerPt, iconRadius, iconRadius);
      }
    }

    p.restore();
  }

  // --- Border (drawn on top of everything) ---
  QColor borderColor = inCombat ? m_combatColor : m_borderColor;
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(borderColor, inCombat ? 3.0 : 2.0));
  p.drawRect(renderRect);
}

void ChildCompositor::renderDecorations()
{
  if (!mumbleLink() || !mumbleLink()->isConnected()) return;
  if (mumbleLink()->mapId() == 0) return;

  int screenW = m_d3dContext->width();
  int screenH = m_d3dContext->height();
  if (screenW <= 0 || screenH <= 0) return;

  if (!ensureDecorTexture()) return;

  // Ensure premultiplied alpha blend state exists (shared with loading bar)
  if (!m_premulBlend) return;

  auto *ctx = m_d3dContext->context();
  if (!ctx) return;

  // Paint decorations onto QImage
  // Use a region-sized image to minimize upload cost
  // For simplicity, use fullscreen image — decorations span minimap + bigmap
  if (m_decorImage.width() != screenW || m_decorImage.height() != screenH) {
    m_decorImage = QImage(screenW, screenH,
                          QImage::Format_ARGB32_Premultiplied);
  }
  m_decorImage.fill(Qt::transparent);

  QPainter painter(&m_decorImage);
  painter.setRenderHint(QPainter::Antialiasing, true);
  paintDecorations(painter, screenW, screenH);
  painter.end();

  // Upload full image to GPU
  ctx->UpdateSubresource(
      m_decorTex.Get(), 0, nullptr,
      m_decorImage.constBits(),
      static_cast<UINT>(m_decorImage.bytesPerLine()),
      0);

  // Draw fullscreen quad with decoration texture
  float blendFactor[4] = {0, 0, 0, 0};
  ctx->OMSetBlendState(m_premulBlend.Get(), blendFactor, 0xFFFFFFFF);

  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->IASetInputLayout(nullptr);
  ctx->VSSetShader(m_quadVS.Get(), nullptr, 0);
  ctx->PSSetShader(m_quadPS.Get(), nullptr, 0);

  if (m_screenSizeCB) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx->Map(m_screenSizeCB.Get(), 0,
                           D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      float cbData[4] = {
        static_cast<float>(screenW),
        static_cast<float>(screenH),
        0.0f, 0.0f
      };
      memcpy(mapped.pData, cbData, sizeof(cbData));
      ctx->Unmap(m_screenSizeCB.Get(), 0);
    }
    ID3D11Buffer *cbs[] = {m_screenSizeCB.Get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
  }

  ID3D11SamplerState *samplers[] = {m_linearSampler.Get()};
  ctx->PSSetSamplers(0, 1, samplers);

  ctx->PSSetShaderResources(0, 1, m_decorSRV.GetAddressOf());
  ctx->Draw(3, 0);

  // Restore default blend state
  ctx->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

// ============================================================================
// Position Tracking
// ============================================================================

void ChildCompositor::updatePosition()
{
  if (!m_hwnd || !m_gw2Hwnd) return;

  RECT rect = {};
  GetClientRect(m_gw2Hwnd, &rect);
  POINT topLeft = {rect.left, rect.top};
  ClientToScreen(m_gw2Hwnd, &topLeft);

  int width = rect.right - rect.left;
  int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) return;

  // Z-order via OverlayZOrder system — compositor is layer 500 (topmost overlay)
  // findInsertAfter finds the highest sibling below our layer to insert after.
  HWND insertAfter = OverlayZOrder::findInsertAfter(
      m_gw2Hwnd, OverlayZOrder::kLayerCompositor, m_hwnd);
  if (insertAfter == m_hwnd) {
    SetWindowPos(m_hwnd, nullptr, topLeft.x, topLeft.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOZORDER);
  } else if (insertAfter) {
    SetWindowPos(m_hwnd, insertAfter, topLeft.x, topLeft.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else {
    // No lower sibling found — place above GW2
    HWND aboveGw2 = GetNextWindow(m_gw2Hwnd, GW_HWNDPREV);
    if (aboveGw2 && aboveGw2 != m_hwnd) {
      SetWindowPos(m_hwnd, aboveGw2, topLeft.x, topLeft.y, width, height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
      SetWindowPos(m_hwnd, HWND_TOP, topLeft.x, topLeft.y, width, height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
  }

  // Resize D3D11 if needed
  QSize newSize(width, height);
  if (width != m_d3dContext->width() || height != m_d3dContext->height()) {
    m_d3dContext->resize(newSize);

    // Close all shared texture consumers — producers will recreate their
    // textures at the new size (via pollGW2WindowSize), invalidating the
    // old NT handles. Without this, acquireForRead() returns stale staging
    // copies forever because consumer->isOpen() stays true on the dead handle.
    // tryOpenConsumers() on the next render frame will reopen with fresh textures.
    for (const QString &layerKey : m_layerOrder) {
      auto *consumer = m_layers.value(layerKey, nullptr);
      if (consumer && consumer->isOpen()) {
        consumer->shutdown();
        qInfo() << "[DEV][COMPOSITOR] Closed consumer for resize:" << layerKey;
      }
    }

    // Notify feature children of resize
    sendToGrandfather(("RESIZE:" + QString::number(width) + ":" +
                    QString::number(height) + "\n").toUtf8());
    qInfo() << "ChildCompositor: Resized to" << width << "x" << height;
  }
}

// ============================================================================
// WinEventHook
// ============================================================================

void ChildCompositor::installEventHook()
{
  if (m_eventHook || !m_gw2Hwnd) return;

  DWORD threadId = GetWindowThreadProcessId(m_gw2Hwnd, nullptr);

  m_eventHook = SetWinEventHook(
      EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
      nullptr, ChildCompositor::winEventProc,
      m_gw2ProcessId, threadId, WINEVENT_OUTOFCONTEXT);

  m_foregroundHook = SetWinEventHook(
      EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
      nullptr, ChildCompositor::foregroundProc,
      0, 0, WINEVENT_OUTOFCONTEXT);

  if (m_eventHook) s_hookMap.insert(m_eventHook, this);
  if (m_foregroundHook) s_hookMap.insert(m_foregroundHook, this);

  qInfo() << "[DEV][COMPOSITOR] Event hooks installed"
          << "locationHook:" << (m_eventHook ? "OK" : "FAIL")
          << "foregroundHook:" << (m_foregroundHook ? "OK" : "FAIL");
}

void ChildCompositor::uninstallEventHook()
{
  if (m_eventHook) {
    s_hookMap.remove(m_eventHook);
    UnhookWinEvent(m_eventHook);
    m_eventHook = nullptr;
  }
  if (m_foregroundHook) {
    s_hookMap.remove(m_foregroundHook);
    UnhookWinEvent(m_foregroundHook);
    m_foregroundHook = nullptr;
  }
}

void CALLBACK ChildCompositor::winEventProc(
    HWINEVENTHOOK hWinEventHook, DWORD /*event*/, HWND hwnd,
    LONG idObject, LONG /*idChild*/, DWORD /*idEventThread*/,
    DWORD /*dwmsEventTime*/)
{
  if (idObject != OBJID_WINDOW) return;

  auto *self = s_hookMap.value(hWinEventHook, nullptr);
  if (!self || hwnd != self->m_gw2Hwnd) return;

  self->updatePosition();

  // GW2 window is being moved/resized — reset focus-loss debounce
  // to prevent transient foreground changes during resize from
  // triggering focus loss (which hides the overlay).
  self->resetFocusLossDebounce();
}

void CALLBACK ChildCompositor::foregroundProc(
    HWINEVENTHOOK hWinEventHook, DWORD /*event*/, HWND hwnd,
    LONG /*idObject*/, LONG /*idChild*/, DWORD /*idEventThread*/,
    DWORD /*dwmsEventTime*/)
{
  auto *self = s_hookMap.value(hWinEventHook, nullptr);
  if (!self) return;

  bool isOurGW2 = (hwnd == self->m_gw2Hwnd);
  if (isOurGW2 && !self->m_contentVisible) {
    self->m_contentVisible = true;
    ShowWindow(self->m_hwnd, SW_SHOWNOACTIVATE);
    self->updatePosition();
    qInfo() << "[DEV][COMPOSITOR] GW2 FOREGROUND_GAINED — showing window";
  }
  // NOTE: FOREGROUND_LOST is NOT handled here — it's handled by the
  // debounced onFocusChanged(false) in the ChildProcess base class.
  //
  // Previous bug: child process spawns (radial, 3D, etc.) create windows
  // that briefly steal foreground, triggering EVENT_SYSTEM_FOREGROUND.
  // The old code instantly set m_contentVisible=false, hiding the compositor.
  // But Layer 1 (GetForegroundWindow polling) never detected focus loss
  // because GW2 immediately regained foreground, so onFocusChanged(true)
  // never fired, and the compositor stayed hidden permanently.
  //
  // Now: only Layer 1's debounced focus loss triggers the hide. Transient
  // foreground changes (child spawns, system processes) are absorbed by
  // the FOCUS_LOSS_DEBOUNCE_MS (500ms) in ChildProcess::onMumbleDataUpdated.
}

// ============================================================================
// Process Exit Wait
// ============================================================================

void ChildCompositor::registerProcessExitWait()
{
  unregisterProcessExitWait();
  if (m_gw2ProcessId == 0) return;

  m_gw2ProcessHandle = OpenProcess(SYNCHRONIZE, FALSE, m_gw2ProcessId);
  if (!m_gw2ProcessHandle) return;

  RegisterWaitForSingleObject(
      &m_processWaitHandle, m_gw2ProcessHandle,
      ChildCompositor::processExitCallback,
      this, INFINITE, WT_EXECUTEONLYONCE);
}

void ChildCompositor::unregisterProcessExitWait()
{
  if (m_processWaitHandle) {
    UnregisterWaitEx(m_processWaitHandle, INVALID_HANDLE_VALUE);
    m_processWaitHandle = nullptr;
  }
  if (m_gw2ProcessHandle) {
    CloseHandle(m_gw2ProcessHandle);
    m_gw2ProcessHandle = nullptr;
  }
}

void CALLBACK ChildCompositor::processExitCallback(PVOID context,
                                                    BOOLEAN /*timedOut*/)
{
  auto *self = static_cast<ChildCompositor *>(context);
  QMetaObject::invokeMethod(self, [self]() {
    qInfo() << "ChildCompositor: GW2 process exited";
    self->stop();
  }, Qt::QueuedConnection);
}

// ============================================================================
// Hit Testing (Hybrid Click-Through)
// ============================================================================

void ChildCompositor::updateInteractiveRects(const QString &layer,
                                              const QList<QRect> &rects)
{
  if (rects.isEmpty()) {
    m_interactiveRects.remove(layer);
  } else {
    m_interactiveRects[layer] = rects;
  }
}

bool ChildCompositor::isPointInteractive(int x, int y,
                                          QString &outLayer) const
{
  // Check layers top-to-bottom (reverse draw order)
  for (auto it = m_layerOrder.crbegin(); it != m_layerOrder.crend(); ++it) {
    const QString &layer = *it;
    auto rectsIt = m_interactiveRects.constFind(layer);
    if (rectsIt == m_interactiveRects.constEnd()) continue;

    for (const QRect &r : rectsIt.value()) {
      if (r.contains(x, y)) {
        outLayer = layer;
        return true;
      }
    }
  }
  return false;
}

// ============================================================================
// WndProc
// ============================================================================

LRESULT CALLBACK ChildCompositor::windowProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam)
{
  auto *self = reinterpret_cast<ChildCompositor *>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
  case WM_NCHITTEST: {
    if (!self) return HTTRANSPARENT;

    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &pt);

    QString hitLayer;
    if (self->isPointInteractive(pt.x, pt.y, hitLayer)) {
      return HTCLIENT;  // Clickable — intercept input
    }
    return HTTRANSPARENT;  // Pass through to GW2
  }

  case WM_DESTROY:
    return 0;

  default:
    break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

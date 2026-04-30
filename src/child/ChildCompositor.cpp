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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
  m_layerOrder = {"3d", "minimap", "radial", "hud"};

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
      ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
      updatePosition();
    } else {
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
}

void ChildCompositor::onReloadPacks()
{
  // No-op for compositor
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

  // Same window flags as D3D11OverlayWindow but WITHOUT WS_EX_TRANSPARENT
  // initially — we handle click-through via WM_NCHITTEST (hybrid approach)
  DWORD exStyle = WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP |
                  WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

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

  // Initialize D3D11
  if (!m_d3dContext->initialize(m_hwnd, QSize(width, height))) {
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

  // Dev log: emit frame count every 1000 frames (~16s at 62.5Hz)
  static uint64_t s_frameCount = 0;
  s_frameCount++;
  if (s_frameCount % 1000 == 0) {
    int openCount = 0;
    for (auto *c : m_layers) { if (c && c->isOpen()) ++openCount; }
    qInfo() << "[DEV][COMPOSITOR] Frame" << s_frameCount
            << "layers:" << m_layers.size()
            << "open:" << openCount
            << "interactiveRects:" << m_interactiveRects.size();
  }

  // Lazy-open: try to open consumers that haven't connected yet.
  // Producers (feature children) may not be ready at compositor start.
  // Retry every ~2s (125 frames at 62.5Hz) to avoid hammering.
  if (s_frameCount % 125 == 1) {
    tryOpenConsumers();
  }

  m_d3dContext->beginFrame();

  // [TEMP DIAGNOSTIC] Draw shared texture content as-is
  // Also add a subtle green border to confirm compositor draws work
  renderLayers();

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

    ID3D11ShaderResourceView *srv = consumer->acquireForRead(0);
    if (!srv) {
      ++s_acquireFail;
      continue;  // Producer hasn't released yet, skip
    }

    ++s_acquireSuccess;
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->Draw(3, 0);  // Fullscreen triangle

    // Unbind SRV before releasing mutex
    ID3D11ShaderResourceView *nullSRV = nullptr;
    ctx->PSSetShaderResources(0, 1, &nullSRV);

    consumer->releaseAfterRead();
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
  } else if (!isOurGW2 && self->m_contentVisible) {
    // Check if another AIO compositor got focus — don't hide
    DWORD fgPid = 0;
    GetWindowThreadProcessId(hwnd, &fgPid);
    if (fgPid != self->m_gw2ProcessId) {
      self->m_contentVisible = false;
      ShowWindow(self->m_hwnd, SW_HIDE);
      qInfo() << "[DEV][COMPOSITOR] GW2 FOREGROUND_LOST — hiding window";
    }
  }
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

/**
 * @file D3D11OverlayWindow.cpp
 * @brief Native Win32 overlay window with D3D11 + DirectComposition
 * transparency
 *
 * Creates a transparent overlay window that tracks the GW2 game window:
 * 1. Registers a custom Win32 window class
 * 2. Creates a popup window with WS_EX_NOREDIRECTIONBITMAP (DComp compositing)
 * 3. Uses DirectComposition to bind the swap chain with per-pixel alpha
 * 4. Uses WinEventHook for event-driven position tracking
 * 5. Renders when MumbleLink data arrives (signal-driven, synchronized)
 *
 * Transparency approach:
 * - CreateSwapChainForComposition with DXGI_ALPHA_MODE_PREMULTIPLIED
 * - IDCompositionVisual::SetContent binds the swap chain to the window
 * - Clear to {0,0,0,0} makes the window fully transparent
 * - Any non-zero alpha pixels drawn by D3D11 are visible over the game
 *
 * Note: DwmExtendFrameIntoClientArea does NOT work with D3D11 flip-model
 * swap chains. DirectComposition is the only D3D11 method for per-pixel alpha.
 */

#include "D3D11OverlayWindow.h"

#include <QDateTime>
#include <QDebug>

#include "core/MumbleLink.h"
#include "core/OverlayZOrder.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerSettingsManager.h"

#include "DebugOverlayWidget.h"
#include "GlyphAtlas.h"
#include "MarkerPipeline.h"
#include "SpriteBatch.h"
#include "TrailPipeline.h"


// Link DirectComposition for per-pixel alpha compositing
#pragma comment(lib, "dcomp.lib")

// ============================================================================
// Static Members
// ============================================================================

// REVIEW BEFORE BETA: s_hookMap is accessed only from Qt main thread (WinEventHook
// callbacks fire on the thread that installed them). Verify this assumption holds
// when OverlayInstanceManager creates/destroys instances.
QHash<HWINEVENTHOOK, D3D11OverlayWindow *> D3D11OverlayWindow::s_hookMap;

// REVIEW BEFORE BETA: s_instanceCounter never resets — window class names grow
// monotonically (GW2AIO_D3D11Overlay_0, _1, ...). Fine for normal use but review
// if instances are created/destroyed frequently in a session.
int D3D11OverlayWindow::s_instanceCounter = 0;

// ============================================================================
// Constructor / Destructor
// ============================================================================

D3D11OverlayWindow::D3D11OverlayWindow(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumbleLink(mumble) {
  // Generate unique window class name for this instance
  m_windowClassName =
      L"GW2AIO_D3D11Overlay_" + std::to_wstring(s_instanceCounter++);

  // Create debug coordinate overlay (diagnostic)
  m_debugOverlay = new DebugOverlayWidget(mumble);

  // Data-driven rendering only: render on MumbleLink data, not a timer.
  // TacO/Blish HUD proven pattern: only render when fresh data arrives.
  // A gap-filling timer causes jitter by re-rendering stale camera data
  // while GW2 has already moved to a newer frame.

  // Connect to MumbleLink signals
  if (m_mumbleLink) {
    connect(m_mumbleLink, &MumbleLink::connectionChanged, this,
            &D3D11OverlayWindow::onGameConnected);

    // dataUpdated handles deferred overlay creation (lifecycle)
    // AND is the sole render trigger — renders only on fresh MumbleLink data.
    connect(m_mumbleLink, &MumbleLink::dataUpdated, this,
            &D3D11OverlayWindow::onMumbleDataUpdated);
  }
}

D3D11OverlayWindow::~D3D11OverlayWindow() {
  stopTracking();
}

// ============================================================================
// Public API
// ============================================================================

void D3D11OverlayWindow::startTracking() {
  if (m_isTracking) {
    return;
  }

  m_isTracking = true;

  // No render timer — rendering is driven solely by MumbleLink::dataUpdated.
  // This ensures every rendered frame uses fresh data (TacO/Blish pattern).

  // Start high-resolution frame timer (Optimization 2)
  m_frameTimer.start();

  // Create overlay window when game connects (deferred)
  if (m_mumbleLink && m_mumbleLink->isConnected()) {
    onGameConnected(true);
  }
}

void D3D11OverlayWindow::stopTracking() {
  m_isTracking = false;

  // (No render timer to stop — rendering is data-driven only)

  unregisterProcessExitWait();
  uninstallEventHook();
  destroyOverlayWindow();
}

void D3D11OverlayWindow::setClickThrough(bool enabled) {
  m_clickThrough = enabled;

  if (m_hwnd) {
    LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (enabled) {
      exStyle |= WS_EX_TRANSPARENT;
    } else {
      exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);
  }
}

void D3D11OverlayWindow::setMarkerManager(MarkerManager *manager) {
  m_markerManager = manager;
}

void D3D11OverlayWindow::setMarkerSettings(MarkerSettingsManager *settings) {
  m_markerSettings = settings;
}

void D3D11OverlayWindow::setImageCache(ImageCache *cache) {
  m_imageCache = cache;
}

void D3D11OverlayWindow::setQueryContext(const MarkerQueryContext *ctx) {
  m_queryCtx = ctx; // Store for deferred pipeline creation
  qInfo() << "[DEV] D3D11OverlayWindow: setQueryContext called, ctx:"
          << (ctx ? "valid" : "null")
          << "markerPipeline:" << (m_markerPipeline ? "exists" : "null")
          << "trailPipeline:" << (m_trailPipeline ? "exists" : "null");
  if (m_markerPipeline) {
    m_markerPipeline->setQueryContext(ctx);
  }
  if (m_trailPipeline) {
    m_trailPipeline->setQueryContext(ctx);
  }
}

void D3D11OverlayWindow::setRenderingEnabled(bool enabled) {
  if (m_renderingEnabled != enabled) {
    qInfo() << "[DIAG] D3D11Overlay: RENDERING_CHANGED"
            << "enabled:" << enabled
            << "contentVisible:" << m_contentVisible
            << "hwnd:" << m_hwnd;
  }
  m_renderingEnabled = enabled;
}

void D3D11OverlayWindow::toggleDebugOverlay(bool visible) {
  if (!m_debugOverlay) {
    return;
  }

  if (visible) {
    if (m_gw2Hwnd) {
      m_debugOverlay->attachToWindow(reinterpret_cast<WId>(m_gw2Hwnd));
    }
    m_debugOverlay->show();
  } else {
    m_debugOverlay->hide();
  }
}

// ============================================================================
// Game Connection
// ============================================================================

void D3D11OverlayWindow::onGameConnected(bool connected) {
  if (!m_isTracking) {
    return;
  }

  if (connected) {
    // Use guaranteed command-line PID if set; fall back to MumbleLink PID.
    // MumbleLink processId() can contain stale data from a previous session.
    uint32_t pid = m_targetPid;
    if (pid == 0) {
      pid = m_mumbleLink->processId();
    }
    qInfo() << "D3D11Overlay: MumbleLink connected, PID:" << pid
            << "(source:" << (m_targetPid ? "command-line" : "MumbleLink") << ")";

    // Reset stall detection for the new instance
    m_contentVisible = true;
    m_lastUiTick = 0;
    m_lastTickChangeMs = QDateTime::currentMSecsSinceEpoch();

    // Try to create overlay now
    tryCreateOverlay(pid);

  } else {
    unregisterProcessExitWait();
    uninstallEventHook();

    if (m_hwnd) {
      ShowWindow(m_hwnd, SW_HIDE);
    }

    m_gw2Hwnd = nullptr;
    emit gameWindowFound(false);
    qInfo() << "D3D11Overlay: Game disconnected";
  }
}

bool D3D11OverlayWindow::tryCreateOverlay(uint32_t pid) {
  if (!findGW2WindowByPid(pid)) {
    if (!m_windowSearchLogged) {
      qWarning() << "D3D11Overlay: Could not find GW2 window for PID:" << pid;
      m_windowSearchLogged = true;
    }
    return false;
  }
  m_windowSearchLogged = false; // Reset on success — future failures will log again

  if (!m_hwnd) {
    if (!createOverlayWindow()) {
      qCritical() << "D3D11Overlay: Failed to create overlay window";
      return false;
    }
  }

  updatePosition();
  installEventHook();
  m_contentVisible = true; // Ensure rendering resumes after reconnect

  // Re-attach debug overlay position if it was already visible
  if (m_debugOverlay && m_debugOverlay->isVisible()) {
    m_debugOverlay->attachToWindow(reinterpret_cast<WId>(m_gw2Hwnd));
  }

  emit gameWindowFound(true);
  registerProcessExitWait();
  qInfo() << "D3D11Overlay: Tracking started for PID:" << pid;
  return true;
}

// ============================================================================
// Window Creation
// ============================================================================

bool D3D11OverlayWindow::createOverlayWindow() {
  HINSTANCE hInstance = GetModuleHandleW(nullptr);

  // Register unique window class for this instance
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = D3D11OverlayWindow::windowProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr; // No background brush (transparent)
  wc.lpszClassName = m_windowClassName.c_str();

  if (!RegisterClassExW(&wc)) {
    DWORD err = GetLastError();
    // ERROR_CLASS_ALREADY_EXISTS (1410) is OK — reusing after prior instance
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      qCritical() << "D3D11Overlay: RegisterClassEx failed:" << err;
      return false;
    }
  }

  // Create popup window for DirectComposition
  // WS_EX_LAYERED: enables cross-process click-through (hit-test transparency)
  // WS_EX_NOREDIRECTIONBITMAP: DComp handles compositing (no GDI redirection)
  // WS_EX_TRANSPARENT: click-through (input passes to game)
  // WS_EX_NOACTIVATE: don't steal focus from game
  // WS_EX_TOOLWINDOW: hide from taskbar
  // NOTE: NO WS_EX_TOPMOST — z-order managed dynamically in updatePosition()
  DWORD exStyle = WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP |
                  WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

  m_hwnd = CreateWindowExW(
      exStyle, m_windowClassName.c_str(),
      OverlayZOrder::buildTitle(m_zOrderLayer, L"3D").c_str(),
      WS_POPUP, 0, 0, 100, 100, // Will be resized
      nullptr, nullptr, hInstance, this);

  if (!m_hwnd) {
    qCritical() << "D3D11Overlay: CreateWindowEx failed:" << GetLastError();
    return false;
  }

  // Store this pointer for WndProc routing
  SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  // Get GW2 window size for initial overlay sizing
  RECT gw2Rect = {};
  if (m_gw2Hwnd) {
    GetClientRect(reinterpret_cast<HWND>(m_gw2Hwnd), &gw2Rect);
  }

  int width = gw2Rect.right - gw2Rect.left;
  int height = gw2Rect.bottom - gw2Rect.top;
  if (width <= 0)
    width = 1920;
  if (height <= 0)
    height = 1080;

  // Initialize D3D11 context
  if (!m_d3dContext.initialize(m_hwnd, QSize(width, height))) {
    qCritical() << "D3D11Overlay: Failed to initialize D3D11 context";
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    return false;
  }

  // Set up DirectComposition for per-pixel alpha compositing.
  // This binds the composition swap chain (PREMULTIPLIED alpha) to the window.
  if (!setupDirectComposition()) {
    qCritical() << "D3D11Overlay: DirectComposition setup failed";
    m_d3dContext.shutdown();
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    return false;
  }

  ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

  // Initialize rendering pipelines now that D3D11 context is ready
  if (m_markerManager && m_markerSettings && m_imageCache) {
    m_markerPipeline =
        new MarkerPipeline(&m_d3dContext, m_mumbleLink, m_markerManager,
                           m_markerSettings, m_imageCache);
    if (!m_markerPipeline->initialize()) {
      qWarning() << "D3D11Overlay: MarkerPipeline init failed";
      delete m_markerPipeline;
      m_markerPipeline = nullptr;
    }

    m_trailPipeline =
        new TrailPipeline(&m_d3dContext, m_mumbleLink, m_markerManager,
                          m_markerSettings, m_imageCache);
    if (!m_trailPipeline->initialize()) {
      qWarning() << "D3D11Overlay: TrailPipeline init failed";
      delete m_trailPipeline;
      m_trailPipeline = nullptr;
    }

    // Phase 7a: propagate stored query context to newly created pipelines
    if (m_queryCtx) {
      qInfo() << "[DEV] D3D11OverlayWindow: propagating stored query context"
              << "to pipelines (mapId:" << m_queryCtx->mapId << ")";
      if (m_markerPipeline) {
        m_markerPipeline->setQueryContext(m_queryCtx);
      }
      if (m_trailPipeline) {
        m_trailPipeline->setQueryContext(m_queryCtx);
      }
    }
  } else {
    qWarning()
        << "D3D11Overlay: Marker data sources not set — skipping pipelines";
  }

  // Initialize 2D sprite batch and glyph atlas (shared by all text rendering)
  m_spriteBatch = new SpriteBatch(&m_d3dContext);
  if (!m_spriteBatch->initialize()) {
    qWarning() << "D3D11Overlay: SpriteBatch init failed";
    delete m_spriteBatch;
    m_spriteBatch = nullptr;
  }

  m_glyphAtlas = new GlyphAtlas(&m_d3dContext);
  // REVIEW BEFORE BETA: hardcoded "Segoe UI" font — consider ThemeManager
  if (!m_glyphAtlas->build("Segoe UI", 12, true)) {
    qWarning() << "D3D11Overlay: GlyphAtlas build failed";
    delete m_glyphAtlas;
    m_glyphAtlas = nullptr;
  }

  // Pass 2D rendering resources to MarkerPipeline for distance labels
  if (m_markerPipeline && m_spriteBatch && m_glyphAtlas) {
    m_markerPipeline->setSpriteBatch(m_spriteBatch);
    m_markerPipeline->setGlyphAtlas(m_glyphAtlas);
  }

  // Create shared exclusion zone constant buffer (b2)
  createExclusionBuffer();

  qInfo() << "D3D11Overlay: Window created" << width << "x" << height;
  return true;
}

bool D3D11OverlayWindow::setupDirectComposition() {
  // Get DXGI device from D3D11 device for DComp interop
  ComPtr<IDXGIDevice> dxgiDevice;
  HRESULT hr = m_d3dContext.device()->QueryInterface(
      IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
  if (FAILED(hr)) {
    qWarning() << "D3D11Overlay: Failed to get IDXGIDevice:" << Qt::hex << hr;
    return false;
  }

  // Create DirectComposition device
  hr = DCompositionCreateDevice(dxgiDevice.Get(),
                                IID_PPV_ARGS(m_dcompDevice.GetAddressOf()));
  if (FAILED(hr)) {
    qWarning() << "D3D11Overlay: DCompositionCreateDevice failed:" << Qt::hex
               << hr;
    return false;
  }

  // Create composition target for our overlay window
  hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, true,
                                          m_dcompTarget.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "D3D11Overlay: CreateTargetForHwnd failed:" << Qt::hex << hr;
    return false;
  }

  // Create visual and bind the swap chain to it
  hr = m_dcompDevice->CreateVisual(m_dcompVisual.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "D3D11Overlay: CreateVisual failed:" << Qt::hex << hr;
    return false;
  }

  hr = m_dcompVisual->SetContent(m_d3dContext.swapChain());
  if (FAILED(hr)) {
    qWarning() << "D3D11Overlay: SetContent failed:" << Qt::hex << hr;
    return false;
  }

  // Set the visual as the root of the composition target
  hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
  if (FAILED(hr)) {
    qWarning() << "D3D11Overlay: SetRoot failed:" << Qt::hex << hr;
    return false;
  }

  // Commit the composition tree — makes it live
  hr = m_dcompDevice->Commit();
  if (FAILED(hr)) {
    qWarning() << "D3D11Overlay: DComp Commit failed:" << Qt::hex << hr;
    return false;
  }

  qInfo() << "D3D11Overlay: DirectComposition setup complete";
  return true;
}

void D3D11OverlayWindow::destroyOverlayWindow() {
  // Tear down pipelines before D3D11 context
  delete m_markerPipeline;
  m_markerPipeline = nullptr;
  delete m_trailPipeline;
  m_trailPipeline = nullptr;

  // Tear down 2D rendering resources
  delete m_glyphAtlas;
  m_glyphAtlas = nullptr;
  delete m_spriteBatch;
  m_spriteBatch = nullptr;

  // Release DComp resources before D3D11 context shutdown
  m_dcompVisual.Reset();
  m_dcompTarget.Reset();
  m_dcompDevice.Reset();

  m_d3dContext.shutdown();

  if (m_hwnd) {
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
  }
}

// ============================================================================
// Window Position Tracking
// ============================================================================

void D3D11OverlayWindow::updatePosition() {
  if (!m_hwnd || !m_gw2Hwnd) {
    return;
  }

  HWND gw2 = reinterpret_cast<HWND>(m_gw2Hwnd);

  // Verify GW2 window still exists
  if (!IsWindow(gw2)) {
    qWarning() << "D3D11Overlay: GW2 window handle invalid — re-finding";
    m_gw2Hwnd = nullptr;

    uint32_t refindPid = m_targetPid ? m_targetPid
                       : (m_mumbleLink ? m_mumbleLink->processId() : 0);
    if (refindPid > 0) {
      if (!findGW2WindowByPid(refindPid)) {
        ShowWindow(m_hwnd, SW_HIDE);
        return;
      }
      gw2 = reinterpret_cast<HWND>(m_gw2Hwnd);
    } else {
      ShowWindow(m_hwnd, SW_HIDE);
      return;
    }
  }

  RECT rect = {};
  GetClientRect(gw2, &rect);

  POINT topLeft = {rect.left, rect.top};
  ClientToScreen(gw2, &topLeft);

  int width = rect.right - rect.left;
  int height = rect.bottom - rect.top;

  if (width <= 0 || height <= 0) {
    return;
  }

  // Per-tick z-order maintenance: position overlay just above its GW2.
  // TOPMOST promotion is handled exclusively by foregroundProc (event-driven).
  // When hidden (unfocused + hideOnUnfocus), skip z-order and don't re-show.
  if (m_hideOnUnfocus && !m_contentVisible) {
    // Only update size/position (for when GW2 is resized while unfocused),
    // but do NOT show or change z-order.
    SetWindowPos(m_hwnd, nullptr, topLeft.x, topLeft.y, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
  } else {
    HWND insertAfter = GetNextWindow(gw2, GW_HWNDPREV);
    if (insertAfter == m_hwnd) {
      // Already just above our GW2 — just update size/position
      SetWindowPos(m_hwnd, nullptr, topLeft.x, topLeft.y, width, height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOZORDER);
    } else if (insertAfter) {
      // Not above our GW2 — move there
      SetWindowPos(m_hwnd, insertAfter, topLeft.x, topLeft.y, width, height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
      // GW2 is at top — place overlay at top (non-topmost)
      SetWindowPos(m_hwnd, HWND_TOP, topLeft.x, topLeft.y, width, height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
  }

  // Resize D3D11 if needed
  m_d3dContext.resize(QSize(width, height));
}

// ============================================================================
// GW2 Window Finding (reused from OverlayWindow)
// ============================================================================

namespace {
struct EnumWindowsData {
  DWORD targetPid; // 0 = match any ArenaNet game window
  HWND result;
  DWORD foundPid;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
  auto *data = reinterpret_cast<EnumWindowsData *>(lParam);

  DWORD windowPid = 0;
  GetWindowThreadProcessId(hwnd, &windowPid);

  // If targeting specific PID, skip non-matching windows
  // If targetPid is 0, accept any window (MumbleLink context not yet populated)
  if (data->targetPid != 0 && windowPid != data->targetPid) {
    return TRUE; // Continue
  }

  if (!IsWindowVisible(hwnd)) {
    return TRUE; // Continue
  }

  // Only accept actual game windows, not splash/patcher:
  // "ArenaNet_Dx_Window_Class" = DX9 game window
  // "ArenaNet_Gr_Window_Class" = DX11 game window
  // "ArenaNet"                 = splash screen (REJECT)
  wchar_t className[256] = {};
  GetClassNameW(hwnd, className, 256);
  QString windowClass = QString::fromWCharArray(className);

  bool isGameWindow =
      (windowClass == QLatin1String("ArenaNet_Dx_Window_Class") ||
       windowClass == QLatin1String("ArenaNet_Gr_Window_Class"));

  if (isGameWindow) {
    data->result = hwnd;
    data->foundPid = windowPid;
    return FALSE; // Found the game window — stop enumeration
  }

  return TRUE; // Continue
}
} // namespace

bool D3D11OverlayWindow::findGW2WindowByPid(uint32_t pid) {
  EnumWindowsData data = {};
  data.targetPid = static_cast<DWORD>(pid);
  data.result = nullptr;
  data.foundPid = 0;

  EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));

  if (data.result) {
    m_gw2Hwnd = data.result;
    m_gw2ProcessId = data.foundPid; // Use actual PID (input may be 0)
    m_gw2ThreadId = GetWindowThreadProcessId(data.result, nullptr);
    qInfo() << "D3D11Overlay: Found GW2 window — PID:" << data.foundPid;
    return true;
  }

  return false;
}

// ============================================================================
// WinEventHook — Event-Driven Position Tracking
// ============================================================================

void D3D11OverlayWindow::installEventHook() {
  if (m_eventHook || m_gw2ThreadId == 0) {
    return;
  }

  // Track GW2 window move/resize
  m_eventHook =
      SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                      nullptr, D3D11OverlayWindow::winEventProc, m_gw2ProcessId,
                      m_gw2ThreadId, WINEVENT_OUTOFCONTEXT);

  // Track foreground changes (for visibility)
  m_foregroundHook = SetWinEventHook(
      EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
      D3D11OverlayWindow::foregroundProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  // Register both hooks in the map for callback routing
  if (m_eventHook) {
    s_hookMap.insert(m_eventHook, this);
  }
  if (m_foregroundHook) {
    s_hookMap.insert(m_foregroundHook, this);
  }

  qInfo() << "D3D11Overlay: Event hooks installed";
}

void D3D11OverlayWindow::uninstallEventHook() {
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

bool D3D11OverlayWindow::isAnyTrackedGW2Window(HWND hwnd) {
  if (!hwnd) {
    return false;
  }
  for (auto *instance : s_hookMap) {
    if (instance && instance->m_gw2Hwnd &&
        reinterpret_cast<HWND>(instance->m_gw2Hwnd) == hwnd) {
      return true;
    }
  }
  return false;
}

void CALLBACK D3D11OverlayWindow::winEventProc(HWINEVENTHOOK hWinEventHook,
                                               DWORD event, HWND hwnd,
                                               LONG idObject, LONG /*idChild*/,
                                               DWORD /*idEventThread*/,
                                               DWORD /*dwmsEventTime*/) {
  if (idObject != OBJID_WINDOW) {
    return;
  }

  auto *self = s_hookMap.value(hWinEventHook, nullptr);
  if (!self) {
    return;
  }

  if (event == EVENT_OBJECT_LOCATIONCHANGE &&
      hwnd == reinterpret_cast<HWND>(self->m_gw2Hwnd)) {
    self->updatePosition();
  }
}

void CALLBACK D3D11OverlayWindow::foregroundProc(
    HWINEVENTHOOK hWinEventHook, DWORD /*event*/, HWND hwnd,
    LONG /*idObject*/, LONG /*idChild*/, DWORD /*idEventThread*/,
    DWORD /*dwmsEventTime*/) {
  auto *self = s_hookMap.value(hWinEventHook, nullptr);
  if (!self || !self->m_hwnd) {
    return;
  }

  bool isGW2 = (hwnd == reinterpret_cast<HWND>(self->m_gw2Hwnd));
  bool isOverlay = (hwnd == self->m_hwnd);

  if (isGW2 || isOverlay) {
    // This instance's GW2 or overlay got focus — ensure rendering is active.
    self->m_contentVisible = true;
    if (self->m_hideOnUnfocus) {
      // Re-show window that was hidden when focus was lost
      ShowWindow(self->m_hwnd, SW_SHOWNOACTIVATE);
    }
    qInfo() << "[DIAG] D3D11Overlay: FOREGROUND_GAINED hwnd:" << hwnd
            << "isGW2:" << isGW2 << "isOverlay:" << isOverlay;
    QMetaObject::invokeMethod(self, [self]() {
      emit self->focusChanged(true);
    }, Qt::QueuedConnection);
  } else {
    if (self->m_hideOnUnfocus) {
      // TacO mode: hide overlay when this instance's GW2 loses focus
      ShowWindow(self->m_hwnd, SW_HIDE);
      self->m_contentVisible = false;
    } else {
      // Blish mode: keep overlay visible
      self->m_contentVisible = true;
    }
    QMetaObject::invokeMethod(self, [self]() {
      emit self->focusChanged(false);
    }, Qt::QueuedConnection);
    qInfo() << "[DIAG] D3D11Overlay: FOREGROUND_LOST hwnd:" << hwnd
            << "hideOnUnfocus:" << self->m_hideOnUnfocus
            << "contentVisible:" << self->m_contentVisible;
  }
}

// ============================================================================
// Render Loop
// ============================================================================

void D3D11OverlayWindow::onMumbleDataUpdated() {
  if (!m_isTracking) {
    return;
  }

  // Deferred overlay creation: GW2 window wasn't found when connectionChanged
  // fired. MumbleLink data is now arriving, so GW2 window should exist.
  if (!m_gw2Hwnd && m_mumbleLink && m_mumbleLink->isConnected()) {
    uint32_t pid = m_targetPid ? m_targetPid : m_mumbleLink->processId();
    if (!tryCreateOverlay(pid)) {
      return; // Will retry on next MumbleLink signal
    }
  }

  // Render on new MumbleLink data: matches TacO's "poll then render" pattern.
  // This is the sole render trigger — no gap-filling timer.

  // Focus-aware throttle: skip rendering when this instance is unfocused.
  // MumbleLink still polls (at reduced rate) for state monitoring.
  if (!m_renderingEnabled) return;

  onRenderFrame();
}

void D3D11OverlayWindow::onRenderFrame() {
  if (!m_isTracking || !m_d3dContext.isInitialized()) {
    return;
  }

  // Throttle z-order + position refresh to ~15Hz (every 4th tick at 62.5Hz).
  // updatePosition() calls SetWindowPos which triggers DWM recomposition.
  // WinEvent hook handles immediate position changes between refreshes.
  if (++m_positionTickCount % 4 == 0) {
    updatePosition();

    // Z-order management (throttled together with position).
    // D3D11 responsibility: "I am always below the Qt overlay."
    // Qt overlay manages its own position (TOPMOST when GW2 focused,
    // above-GW2 when not). D3D11 anchors itself below Qt.
    // This guarantees: GW2 → D3D11 (trails) → Qt (markers + dot)
    // regardless of focus state — no race condition.
    // Previously ran every frame (62.5Hz) causing excessive DWM recomposition.
    //
    // Periodic cache invalidation: D3D11 has no foreground hook, so the
    // z-order cache can become stale after focus transitions (Windows
    // reshuffles z-order). Invalidate every ~1s so the overlay re-asserts.
    if (m_positionTickCount % 60 == 0) {
      m_lastInsertAfterHwnd = nullptr;
    }
    if (m_hwnd && m_gw2Hwnd) {
      HWND qtHwnd = m_qtOverlayHwnd;

      if (qtHwnd) {
        // Single-process mode: anchor below paired Qt overlay.
        // Only call SetWindowPos if the anchor changed.
        if (qtHwnd != m_lastInsertAfterHwnd) {
          ::SetWindowPos(m_hwnd, qtHwnd, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
          m_lastInsertAfterHwnd = qtHwnd;
        }
      } else {
        // Multi-process mode: use layer-aware z-ordering.
        // Find the highest AIO sibling below our layer and insert above it.
        HWND gw2 = reinterpret_cast<HWND>(m_gw2Hwnd);
        HWND insertAfter = OverlayZOrder::findInsertAfter(
            gw2, m_zOrderLayer, m_hwnd);
        if (!insertAfter) {
          // No lower sibling — insert just above GW2
          insertAfter = ::GetNextWindow(gw2, GW_HWNDPREV);
        }
        // Cache check: skip SetWindowPos if insertion point unchanged.
        // Redundant SetWindowPos triggers DWM recomposition → flicker.
        if (insertAfter && insertAfter != m_hwnd &&
            insertAfter != m_lastInsertAfterHwnd) {
          ::SetWindowPos(m_hwnd, insertAfter, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
          m_lastInsertAfterHwnd = insertAfter;
        } else if (!insertAfter || insertAfter == m_hwnd) {
          if (m_lastInsertAfterHwnd != HWND_TOP) {
            ::SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            m_lastInsertAfterHwnd = HWND_TOP;
          }
        }
      }
    }
  }

  // Loading-screen-aware visibility (replaces pure uiTick stall detection).
  //
  // OLD approach: hide when uiTick stalls for 333ms. This caused flickering
  // during normal gameplay because GPU load from rendering briefly stalls
  // uiTick, triggering false STALL_TOGGLE at ~3Hz.
  //
  // NEW approach (matches OverlayWindow pattern at line 713-719):
  // 1. mapId == 0 → character select, not in-game
  // 2. position == (0,0,0) → loading screen, pre-spawn
  // 3. uiTick stall > 2s → edge-case fallback (legitimate disconnect/crash)
  //
  // During normal gameplay, brief GPU stalls no longer hide content.
  if (m_mumbleLink && m_mumbleLink->isConnected()) {
    uint32_t currentTick = m_mumbleLink->uiTick();
    qint64 now = m_frameTimer.elapsed();

    if (currentTick != m_lastUiTick) {
      m_lastUiTick = currentTick;
      m_lastTickChangeMs = now;
    }

    // Primary: detect actual loading screens via game state
    bool hasValidMap = m_mumbleLink->mapId() > 0;
    bool hasValidPosition = (m_mumbleLink->playerX() != 0.0f ||
                             m_mumbleLink->playerY() != 0.0f ||
                             m_mumbleLink->playerZ() != 0.0f);
    // Secondary: long stall fallback (2s) for edge cases only
    bool longStall = (now - m_lastTickChangeMs) >= 2000;

    bool shouldShow = hasValidMap && hasValidPosition && !longStall;
    if (shouldShow != m_contentVisible) {
      qInfo() << "[DIAG] D3D11Overlay: VISIBILITY_TOGGLE"
              << "visible:" << shouldShow
              << "mapId:" << m_mumbleLink->mapId()
              << "validPos:" << hasValidPosition
              << "stallMs:" << (now - m_lastTickChangeMs)
              << "longStall:" << longStall
              << "uiTick:" << currentTick;
      m_contentVisible = shouldShow;
      if (!shouldShow) {
        m_needsClearFrame = true; // Render one transparent frame to erase
      }
    }
  }

  if (!m_contentVisible) {
    // Push one clear (transparent) frame to erase markers/trails from screen.
    // Without this, the swap chain retains the last rendered frame.
    if (m_needsClearFrame) {
      m_needsClearFrame = false;
      m_d3dContext.beginFrame(); // Clears to {0,0,0,0} transparent
      m_d3dContext.endFrame();   // Presents the clear frame
    }
    return;
  }


  render();
}

void D3D11OverlayWindow::render() {
  // TacO architecture: use raw MumbleLink data directly.
  // NO camera interpolation, extrapolation, or prediction.
  // Pipelines read MumbleLink camera/player data directly.
  // Any camera prediction error displaces markers relative to the game world.
  // TacO's MumbleLink.cpp confirms: inter = 1.0 (interpolation disabled).

  // Pre-load textures BEFORE the frame begins.
  // D3D11 textures created during an active frame silently fail to render.
  if (m_markerPipeline) {
    m_markerPipeline->preloadTextures();
  }
  if (m_trailPipeline) {
    m_trailPipeline->preloadTextures();
  }

  // Sync render settings from MarkerSettingsManager to pipelines
  if (m_markerSettings) {
    float maxDist = static_cast<float>(m_markerSettings->maxRenderDistance());
    float overlayOpacity =
        static_cast<float>(m_markerSettings->overlayOpacity());
    float minimapOpacity =
        static_cast<float>(m_markerSettings->minimapOpacity());

    // Rendering layer toggles (Main gates all others)
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

      // Rebuild glyph atlas if font size changed
      int fontSize = m_markerSettings->distanceFontSize();
      if (m_glyphAtlas && m_glyphAtlas->currentFontSize() != fontSize) {
        // REVIEW BEFORE BETA: hardcoded "Segoe UI" font — consider ThemeManager
        m_glyphAtlas->build("Segoe UI", fontSize, true);
      }
    }
    // TrailPipeline reads maxRenderDistance directly from m_markerSettings
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

  m_d3dContext.beginFrame();

  // Update and bind exclusion zones to PS register b2 (before pipeline draws)
  updateAndBindExclusionZones();

  // 1. Render 3D trail meshes (behind markers)
  if (m_trailPipeline) {
    m_trailPipeline->render();
  }

  // 2. Render 3D marker billboards (on top of trails)
  if (m_markerPipeline) {
    m_markerPipeline->render();
  }

  // 3. Render minimap/bigmap trails (GPU — TacO approach)
  if (m_trailPipeline) {
    m_trailPipeline->renderMinimap();
  }

  // REVIEW BEFORE BETA: OverlayMenuRenderer not yet implemented
  // TODO phase A: OverlayMenuRenderer::render()

  m_d3dContext.endFrame();
}

// ============================================================================
// Exclusion Zone Buffer
// ============================================================================

bool D3D11OverlayWindow::createExclusionBuffer() {
  if (!m_d3dContext.isInitialized()) {
    return false;
  }

  D3D11_BUFFER_DESC cbDesc = {};
  cbDesc.ByteWidth = sizeof(ExclusionCB);
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  HRESULT hr = m_d3dContext.device()->CreateBuffer(
      &cbDesc, nullptr, m_exclusionCB.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "D3D11Overlay: Failed to create exclusion CB, hr:" << hr;
    return false;
  }

  qInfo() << "D3D11Overlay: Exclusion zone CB created"
          << "(" << sizeof(ExclusionCB) << "bytes)";
  return true;
}

void D3D11OverlayWindow::updateAndBindExclusionZones() {
  if (!m_exclusionCB || !m_d3dContext.isInitialized()) {
    return;
  }

  auto *ctx = m_d3dContext.context();
  if (!ctx) {
    return;
  }

  float screenW = static_cast<float>(m_d3dContext.width());
  float screenH = static_cast<float>(m_d3dContext.height());

  if (screenW <= 0.0f || screenH <= 0.0f) {
    return;
  }

  // Build zone list: predefined zones first, then custom
  ExclusionCB cb = {};
  cb.screenWidth = screenW;
  cb.screenHeight = screenH;
  cb.zoneCount = 0;

  // Read settings (default to enabled if no settings manager yet)
  bool enabled = true;
  bool minimapOn = true;
  bool skillBarOn = true;
  bool chatOn = true;
  float fadeEdge = 0.02f;

  if (m_markerSettings) {
    enabled = m_markerSettings->exclusionEnabled();
    minimapOn = m_markerSettings->minimapZoneEnabled();
    skillBarOn = m_markerSettings->skillBarZoneEnabled();
    chatOn = m_markerSettings->chatZoneEnabled();
    fadeEdge = m_markerSettings->exclusionFadeEdge();
  }

  cb.fadeEdge = fadeEdge;

  // If exclusion zones are globally disabled, send empty zones
  if (!enabled) {
    D3D11_MAPPED_SUBRESOURCE emptyMapped;
    HRESULT hr2 = ctx->Map(m_exclusionCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                           &emptyMapped);
    if (SUCCEEDED(hr2)) {
      memcpy(emptyMapped.pData, &cb, sizeof(ExclusionCB));
      ctx->Unmap(m_exclusionCB.Get(), 0);
    }
    ID3D11Buffer *cbs[] = {m_exclusionCB.Get()};
    ctx->PSSetConstantBuffers(2, 1, cbs);
    return;
  }

  if (minimapOn && m_mumbleLink && m_mumbleLink->isConnected()) {
    const auto &compass = m_mumbleLink->minimapData();
    if (compass.compassWidth > 0 && compass.compassHeight > 0 &&
        cb.zoneCount < kMaxExclusionZones) {
      float cw = static_cast<float>(compass.compassWidth) / screenW;
      float ch = static_cast<float>(compass.compassHeight) / screenH;

      float cx, cy;
      if (m_mumbleLink->isMinimapTopRight()) {
        // Top-right corner (flush to top edge)
        cx = 1.0f - cw;
        cy = 0.0f;
      } else {
        // Bottom-right corner (default)
        // GW2 has a bottom UI bar (~36px at 1080p) that offsets the minimap
        constexpr float kBottomBarPx = 36.0f;
        float bottomOffset = kBottomBarPx / screenH;
        cx = 1.0f - cw;
        cy = 1.0f - ch - bottomOffset;
      }

      cb.zones[cb.zoneCount] = {cx, cy, cw, ch};
      cb.zoneCount++;
    }
  }

  // --- Predefined zone: Skill Bar ---
  // GW2's skill bar has a fixed pixel size (~768×96px at standard UI scale).
  // Convert to percentage but cap so it doesn't grow beyond the max pixel size.
  if (skillBarOn && cb.zoneCount < kMaxExclusionZones) {
    if (m_markerSettings &&
        m_markerSettings->hasPredefinedOverride("SkillBar")) {
      // Use user-customized position
      auto ov = m_markerSettings->predefinedOverride("SkillBar");
      cb.zones[cb.zoneCount] = {ov.x, ov.y, ov.w, ov.h};
    } else {
      // Default position
      constexpr float kSkillBarMaxW = 768.0f;
      constexpr float kSkillBarMaxH = 96.0f;
      float sbW = qMin(0.40f, kSkillBarMaxW / screenW);
      float sbH = qMin(0.09f, kSkillBarMaxH / screenH);
      float sbX = 0.5f - sbW / 2.0f;
      float sbY = 1.0f - sbH;
      cb.zones[cb.zoneCount] = {sbX, sbY, sbW, sbH};
    }
    cb.zoneCount++;
  }

  // --- Predefined zone: Chat Box ---
  // Bottom-left corner, estimated ~30% width × ~25% height
  if (chatOn && cb.zoneCount < kMaxExclusionZones) {
    if (m_markerSettings && m_markerSettings->hasPredefinedOverride("Chat")) {
      auto ov = m_markerSettings->predefinedOverride("Chat");
      cb.zones[cb.zoneCount] = {ov.x, ov.y, ov.w, ov.h};
    } else {
      cb.zones[cb.zoneCount] = {0.0f, 0.75f, 0.28f, 0.25f};
    }
    cb.zoneCount++;
  }

  // --- Custom zones from settings ---
  if (m_markerSettings) {
    const auto &customZones = m_markerSettings->customZones();
    for (const auto &zone : customZones) {
      if (cb.zoneCount >= kMaxExclusionZones)
        break;
      cb.zones[cb.zoneCount] = {zone.x, zone.y, zone.w, zone.h};
      cb.zoneCount++;
    }
  }

  // Map and update the CB
  D3D11_MAPPED_SUBRESOURCE mapped;
  HRESULT hr =
      ctx->Map(m_exclusionCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (SUCCEEDED(hr)) {
    memcpy(mapped.pData, &cb, sizeof(ExclusionCB));
    ctx->Unmap(m_exclusionCB.Get(), 0);
  }

  // Bind to PS register b2 (shared by both marker and trail shaders)
  ID3D11Buffer *cbs[] = {m_exclusionCB.Get()};
  ctx->PSSetConstantBuffers(2, 1, cbs);
}

// ============================================================================
// Win32 Message Handler
// ============================================================================

LRESULT CALLBACK D3D11OverlayWindow::windowProc(HWND hwnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam) {
  auto *self = reinterpret_cast<D3D11OverlayWindow *>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
  case WM_SIZE: {
    if (self && self->m_d3dContext.isInitialized()) {
      int width = LOWORD(lParam);
      int height = HIWORD(lParam);
      if (width > 0 && height > 0) {
        self->m_d3dContext.resize(QSize(width, height));
      }
    }
    return 0;
  }

  case WM_DESTROY:
    return 0;

  case WM_NCHITTEST:
    // When click-through is disabled, allow mouse interaction
    if (self && !self->m_clickThrough) {
      return HTCLIENT;
    }
    return HTTRANSPARENT;

  default:
    break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// =============================================================================
// Process Exit Wait (RegisterWaitForSingleObject — instant GW2 exit detection)
// =============================================================================

void D3D11OverlayWindow::registerProcessExitWait() {
  unregisterProcessExitWait(); // Clean up any previous wait

  if (m_gw2ProcessId == 0) {
    return;
  }

  m_gw2ProcessHandle = OpenProcess(SYNCHRONIZE, FALSE, m_gw2ProcessId);
  if (!m_gw2ProcessHandle) {
    qWarning() << "D3D11Overlay: Failed to open GW2 process for exit wait,"
               << "PID:" << m_gw2ProcessId << "Error:" << GetLastError();
    return;
  }

  if (!RegisterWaitForSingleObject(&m_processWaitHandle, m_gw2ProcessHandle,
                                   processExitCallback, this, INFINITE,
                                   WT_EXECUTEONLYONCE)) {
    qWarning() << "D3D11Overlay: RegisterWaitForSingleObject failed,"
               << "Error:" << GetLastError();
    CloseHandle(m_gw2ProcessHandle);
    m_gw2ProcessHandle = nullptr;
    return;
  }

  qInfo() << "D3D11Overlay: Registered process exit wait for PID:"
          << m_gw2ProcessId;
}

void D3D11OverlayWindow::unregisterProcessExitWait() {
  if (m_processWaitHandle) {
    // Blocking variant: waits for any in-flight thread pool callback to
    // complete before returning. Without this, the callback can access 'this'
    // after stack destruction → ACCESS_VIOLATION in Qt6Core.dll.
    UnregisterWaitEx(m_processWaitHandle, INVALID_HANDLE_VALUE);
    m_processWaitHandle = nullptr;
  }
  if (m_gw2ProcessHandle) {
    CloseHandle(m_gw2ProcessHandle);
    m_gw2ProcessHandle = nullptr;
  }
}

void CALLBACK D3D11OverlayWindow::processExitCallback(PVOID context,
                                                      BOOLEAN timedOut) {
  Q_UNUSED(timedOut);
  auto *self = static_cast<D3D11OverlayWindow *>(context);
  if (!self) {
    return;
  }

  // Marshal to main thread — callback runs on Windows thread pool
  QMetaObject::invokeMethod(self, "onGw2ProcessExited", Qt::QueuedConnection);
}

void D3D11OverlayWindow::onGw2ProcessExited() {
  if (!m_isTracking) {
    return;
  }

  qInfo() << "D3D11Overlay: GW2 process exited (instant detection)"
          << "— hiding overlay";

  unregisterProcessExitWait();
  uninstallEventHook();

  if (m_hwnd) {
    ShowWindow(m_hwnd, SW_HIDE);
  }

  // Hide debug overlay
  if (m_debugOverlay) {
    m_debugOverlay->hide();
  }

  m_gw2Hwnd = nullptr;
  m_contentVisible = false;
  emit gameWindowFound(false);
}

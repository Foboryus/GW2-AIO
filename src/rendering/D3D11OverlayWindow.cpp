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
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerSettingsManager.h"

#include "DebugOverlayWidget.h"
#include "GlyphAtlas.h"
#include "MarkerPipeline.h"
#include "SpriteBatch.h"
#include "TrailPipeline.h"

#include "ui/OverlayWindow.h"

// Link DirectComposition for per-pixel alpha compositing
#pragma comment(lib, "dcomp.lib")

// ============================================================================
// Static Members
// ============================================================================

D3D11OverlayWindow *D3D11OverlayWindow::s_instance = nullptr;
bool D3D11OverlayWindow::s_windowClassRegistered = false;
const wchar_t *D3D11OverlayWindow::kWindowClassName = L"GW2AIO_D3D11Overlay";

// ============================================================================
// Constructor / Destructor
// ============================================================================

D3D11OverlayWindow::D3D11OverlayWindow(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumbleLink(mumble) {
  s_instance = this;

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

  if (s_instance == this) {
    s_instance = nullptr;
  }
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
    // PID may be 0 if MumbleLink context not yet populated, or if GW2
    // writes to a custom segment. findGW2WindowByPid handles PID=0 by
    // matching any ArenaNet game window (same approach as TacO/OverlayWindow).
    uint32_t pid = m_mumbleLink->processId();
    qInfo() << "D3D11Overlay: MumbleLink connected, PID:" << pid;

    // Reset stall detection for the new instance
    m_contentVisible = true;
    m_lastUiTick = 0;
    m_lastTickChangeMs = QDateTime::currentMSecsSinceEpoch();

    // Try to create overlay now. If it fails (GW2 window not yet available),
    // the next mapChanged or cameraChanged signal will trigger
    // onMumbleDataUpdated which retries overlay creation.
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
    qWarning() << "D3D11Overlay: Could not find GW2 window for PID:" << pid;
    return false;
  }

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

  // Register window class (once)
  if (!s_windowClassRegistered) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = D3D11OverlayWindow::windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // No background brush (transparent)
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
      qCritical() << "D3D11Overlay: RegisterClassEx failed:" << GetLastError();
      return false;
    }
    s_windowClassRegistered = true;
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

  m_hwnd = CreateWindowExW(exStyle, kWindowClassName, L"GW2 AIO Overlay",
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

    if (m_mumbleLink && m_mumbleLink->processId() > 0) {
      if (!findGW2WindowByPid(m_mumbleLink->processId())) {
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

  // TacO z-order approach: position overlay directly above the GW2 window
  // in the z-order. This ensures the overlay appears on top of GW2 but
  // BEHIND any other application windows (e.g., browser, PDF reader).
  HWND insertAfter = GetNextWindow(gw2, GW_HWNDPREV);
  if (insertAfter == m_hwnd) {
    // Already positioned correctly — just update size/position
    SetWindowPos(m_hwnd, nullptr, topLeft.x, topLeft.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOZORDER);
  } else if (insertAfter) {
    // Position just above GW2 (behind the window above GW2)
    SetWindowPos(m_hwnd, insertAfter, topLeft.x, topLeft.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else {
    // GW2 is already topmost — fallback to TOPMOST
    SetWindowPos(m_hwnd, HWND_TOPMOST, topLeft.x, topLeft.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

  qInfo() << "D3D11Overlay: Event hooks installed";
}

void D3D11OverlayWindow::uninstallEventHook() {
  if (m_eventHook) {
    UnhookWinEvent(m_eventHook);
    m_eventHook = nullptr;
  }
  if (m_foregroundHook) {
    UnhookWinEvent(m_foregroundHook);
    m_foregroundHook = nullptr;
  }
}

void CALLBACK D3D11OverlayWindow::winEventProc(HWINEVENTHOOK /*hWinEventHook*/,
                                               DWORD event, HWND hwnd,
                                               LONG idObject, LONG /*idChild*/,
                                               DWORD /*idEventThread*/,
                                               DWORD /*dwmsEventTime*/) {
  if (!s_instance || idObject != OBJID_WINDOW) {
    return;
  }

  if (event == EVENT_OBJECT_LOCATIONCHANGE &&
      hwnd == reinterpret_cast<HWND>(s_instance->m_gw2Hwnd)) {
    s_instance->updatePosition();
  }
}

void CALLBACK D3D11OverlayWindow::foregroundProc(
    HWINEVENTHOOK /*hWinEventHook*/, DWORD /*event*/, HWND hwnd,
    LONG /*idObject*/, LONG /*idChild*/, DWORD /*idEventThread*/,
    DWORD /*dwmsEventTime*/) {
  if (!s_instance || !s_instance->m_hwnd) {
    return;
  }

  bool isGW2 = (hwnd == reinterpret_cast<HWND>(s_instance->m_gw2Hwnd));
  bool isOverlay = (hwnd == s_instance->m_hwnd);

  if (isGW2 || isOverlay) {
    // GW2 or overlay got focus — ensure rendering is active.
    // Z-order is managed per-frame in onRenderFrame() (TacO pattern).
    s_instance->m_contentVisible = true;
  } else {
    if (s_instance->m_hideOnUnfocus) {
      // TacO mode: hide overlay when GW2 loses focus
      ShowWindow(s_instance->m_hwnd, SW_HIDE);
      s_instance->m_contentVisible = false;
    } else {
      // Blish mode: keep overlay visible
      s_instance->m_contentVisible = true;
    }
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
    uint32_t pid = m_mumbleLink->processId();
    if (!tryCreateOverlay(pid)) {
      return; // Will retry on next MumbleLink signal
    }
  }

  // Render on new MumbleLink data: matches TacO's "poll then render" pattern.
  // This is the sole render trigger — no gap-filling timer.
  onRenderFrame();
}

void D3D11OverlayWindow::onRenderFrame() {
  if (!m_isTracking || !m_d3dContext.isInitialized()) {
    return;
  }

  // No frame-rate guard needed — rendering is driven solely by
  // MumbleLink::dataUpdated (~50Hz). Every frame uses fresh data.

  // TacO approach: track wall-clock time since uiTick last changed.
  // When GW2 is on a loading screen or character select, uiTick freezes.
  // After 333ms stale (TacO's proven threshold), hide markers/trails.
  if (m_mumbleLink && m_mumbleLink->isConnected()) {
    uint32_t currentTick = m_mumbleLink->uiTick();
    qint64 now = m_frameTimer.elapsed();

    if (currentTick != m_lastUiTick) {
      m_lastUiTick = currentTick;
      m_lastTickChangeMs = now;
    }

    bool shouldShow = (now - m_lastTickChangeMs) < kStallMs;
    if (shouldShow != m_contentVisible) {
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

  // Per-frame z-order management.
  // D3D11 responsibility: "I am always below the Qt overlay."
  // Qt overlay manages its own position (TOPMOST when GW2 focused,
  // above-GW2 when not). D3D11 anchors itself below Qt every frame.
  // This guarantees: GW2 → D3D11 (trails) → Qt (markers + dot)
  // regardless of focus state — no race condition.
  if (m_hwnd && m_gw2Hwnd) {
    HWND qtHwnd = OverlayWindow::qtOverlayHwnd();

    if (qtHwnd) {
      // SetWindowPos(A, B) → A goes BELOW B in z-order.
      // D3D11 goes below Qt → Qt is always on top of D3D11.
      ::SetWindowPos(m_hwnd, qtHwnd, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
      // Qt overlay not active yet — fall back to independent z-order
      HWND gw2 = reinterpret_cast<HWND>(m_gw2Hwnd);
      HWND foreground = ::GetForegroundWindow();

      if (foreground == gw2 || foreground == m_hwnd) {
        ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      } else {
        HWND wnd = ::GetNextWindow(gw2, GW_HWNDPREV);
        if (wnd && wnd != m_hwnd) {
          ::SetWindowPos(m_hwnd, wnd, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
      }
    }
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

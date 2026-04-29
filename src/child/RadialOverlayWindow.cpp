/**
 * @file RadialOverlayWindow.cpp
 * @brief Lightweight D3D11 overlay for the radial menu child process
 *
 * Adapted from D3D11OverlayWindow — same Win32/DComp/WinEventHook infrastructure,
 * but no MarkerPipeline, TrailPipeline, SpriteBatch, GlyphAtlas, or ExclusionZones.
 * Rendering is driven by a pluggable callback set via setRenderCallback().
 *
 * See D3D11OverlayWindow.cpp for detailed commentary on each technique.
 */

#include "RadialOverlayWindow.h"

#include <QDateTime>
#include <QDebug>

#include "core/MumbleLink.h"
#include "core/OverlayZOrder.h"

// Link DirectComposition for per-pixel alpha compositing
#pragma comment(lib, "dcomp.lib")

// ============================================================================
// Static Members
// ============================================================================

QHash<HWINEVENTHOOK, RadialOverlayWindow *> RadialOverlayWindow::s_hookMap;
int RadialOverlayWindow::s_instanceCounter = 0;

// ============================================================================
// Constructor / Destructor
// ============================================================================

RadialOverlayWindow::RadialOverlayWindow(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumbleLink(mumble) {
  m_windowClassName =
      L"GW2AIO_RadialOverlay_" + std::to_wstring(s_instanceCounter++);

  if (m_mumbleLink) {
    connect(m_mumbleLink, &MumbleLink::connectionChanged, this,
            &RadialOverlayWindow::onGameConnected);
    connect(m_mumbleLink, &MumbleLink::dataUpdated, this,
            &RadialOverlayWindow::onMumbleDataUpdated);
  }
}

RadialOverlayWindow::~RadialOverlayWindow() { stopTracking(); }

// ============================================================================
// Public API
// ============================================================================

void RadialOverlayWindow::startTracking() {
  if (m_isTracking) {
    return;
  }

  m_isTracking = true;
  m_frameTimer.start();

  if (m_mumbleLink && m_mumbleLink->isConnected()) {
    onGameConnected(true);
  }
}

void RadialOverlayWindow::stopTracking() {
  m_isTracking = false;
  unregisterProcessExitWait();
  uninstallEventHook();
  destroyOverlayWindow();
}

void RadialOverlayWindow::setRenderingEnabled(bool enabled) {
  if (m_renderingEnabled != enabled) {
    qInfo() << "[DIAG] RadialOverlay: RENDERING_CHANGED"
            << "enabled:" << enabled
            << "contentVisible:" << m_contentVisible;
  }
  m_renderingEnabled = enabled;
}

void RadialOverlayWindow::setWheelNeedsRendering(bool needs) {
  m_wheelNeedsRendering = needs;
}

void RadialOverlayWindow::setRenderCallback(
    std::function<bool(D3D11Context *)> callback) {
  m_renderCallback = std::move(callback);
}

void RadialOverlayWindow::setIdleCallback(std::function<void()> callback) {
  m_idleCallback = std::move(callback);
}

// ============================================================================
// Game Connection
// ============================================================================

void RadialOverlayWindow::onGameConnected(bool connected) {
  if (!m_isTracking) {
    return;
  }

  if (connected) {
    uint32_t pid = m_targetPid;
    if (pid == 0) {
      pid = m_mumbleLink->processId();
    }
    qInfo() << "RadialOverlay: MumbleLink connected, PID:" << pid
            << "(source:" << (m_targetPid ? "command-line" : "MumbleLink")
            << ")";

    m_contentVisible = true;
    m_lastUiTick = 0;
    m_lastTickChangeMs = QDateTime::currentMSecsSinceEpoch();

    tryCreateOverlay(pid);
  } else {
    unregisterProcessExitWait();
    uninstallEventHook();

    if (m_hwnd) {
      ShowWindow(m_hwnd, SW_HIDE);
    }

    m_gw2Hwnd = nullptr;
    emit gameWindowFound(false);
    qInfo() << "RadialOverlay: Game disconnected";
  }
}

bool RadialOverlayWindow::tryCreateOverlay(uint32_t pid) {
  if (!findGW2WindowByPid(pid)) {
    if (!m_windowSearchLogged) {
      qWarning() << "RadialOverlay: Could not find GW2 window for PID:" << pid;
      m_windowSearchLogged = true;
    }
    return false;
  }
  m_windowSearchLogged = false;

  if (!m_hwnd) {
    if (!createOverlayWindow()) {
      qCritical() << "RadialOverlay: Failed to create overlay window";
      return false;
    }
  }

  updatePosition();
  installEventHook();
  m_contentVisible = true;

  emit gameWindowFound(true);
  registerProcessExitWait();
  qInfo() << "RadialOverlay: Tracking started for PID:" << pid;
  return true;
}

// ============================================================================
// Window Creation
// ============================================================================

bool RadialOverlayWindow::createOverlayWindow() {
  HINSTANCE hInstance = GetModuleHandleW(nullptr);

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = RadialOverlayWindow::windowProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = m_windowClassName.c_str();

  if (!RegisterClassExW(&wc)) {
    DWORD err = GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      qCritical() << "RadialOverlay: RegisterClassEx failed:" << err;
      return false;
    }
  }

  // WS_EX_LAYERED: enables cross-process click-through
  // WS_EX_NOREDIRECTIONBITMAP: DComp handles compositing
  // WS_EX_TRANSPARENT: click-through (input passes to game)
  // WS_EX_NOACTIVATE: don't steal focus from game
  // WS_EX_TOOLWINDOW: hide from taskbar
  DWORD exStyle = WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP |
                  WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

  m_hwnd = CreateWindowExW(
      exStyle, m_windowClassName.c_str(),
      OverlayZOrder::buildTitle(OverlayZOrder::kLayerRadial, L"Radial").c_str(),
      WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, hInstance, this);

  if (!m_hwnd) {
    qCritical() << "RadialOverlay: CreateWindowEx failed:" << GetLastError();
    return false;
  }

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

  if (!m_d3dContext.initialize(m_hwnd, QSize(width, height))) {
    qCritical() << "RadialOverlay: Failed to initialize D3D11 context";
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    return false;
  }

  if (!setupDirectComposition()) {
    qCritical() << "RadialOverlay: DirectComposition setup failed";
    m_d3dContext.shutdown();
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    return false;
  }

  ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

  qInfo() << "RadialOverlay: Window created" << width << "x" << height;
  return true;
}

bool RadialOverlayWindow::setupDirectComposition() {
  ComPtr<IDXGIDevice> dxgiDevice;
  HRESULT hr = m_d3dContext.device()->QueryInterface(
      IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
  if (FAILED(hr)) {
    qWarning() << "RadialOverlay: Failed to get IDXGIDevice:" << Qt::hex << hr;
    return false;
  }

  hr = DCompositionCreateDevice(dxgiDevice.Get(),
                                IID_PPV_ARGS(m_dcompDevice.GetAddressOf()));
  if (FAILED(hr)) {
    qWarning() << "RadialOverlay: DCompositionCreateDevice failed:" << Qt::hex
               << hr;
    return false;
  }

  hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, true,
                                          m_dcompTarget.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "RadialOverlay: CreateTargetForHwnd failed:" << Qt::hex << hr;
    return false;
  }

  hr = m_dcompDevice->CreateVisual(m_dcompVisual.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "RadialOverlay: CreateVisual failed:" << Qt::hex << hr;
    return false;
  }

  hr = m_dcompVisual->SetContent(m_d3dContext.swapChain());
  if (FAILED(hr)) {
    qWarning() << "RadialOverlay: SetContent failed:" << Qt::hex << hr;
    return false;
  }

  hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
  if (FAILED(hr)) {
    qWarning() << "RadialOverlay: SetRoot failed:" << Qt::hex << hr;
    return false;
  }

  hr = m_dcompDevice->Commit();
  if (FAILED(hr)) {
    qWarning() << "RadialOverlay: DComp Commit failed:" << Qt::hex << hr;
    return false;
  }

  qInfo() << "RadialOverlay: DirectComposition setup complete";
  return true;
}

void RadialOverlayWindow::destroyOverlayWindow() {
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

void RadialOverlayWindow::updatePosition() {
  if (!m_hwnd || !m_gw2Hwnd) {
    return;
  }

  HWND gw2 = reinterpret_cast<HWND>(m_gw2Hwnd);

  if (!IsWindow(gw2)) {
    qWarning() << "RadialOverlay: GW2 window handle invalid — re-finding";
    m_gw2Hwnd = nullptr;

    uint32_t refindPid =
        m_targetPid ? m_targetPid
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

  // Layer-based z-order: insert above the highest lower-layer sibling
  if (m_hideOnUnfocus && !m_contentVisible) {
    SetWindowPos(m_hwnd, nullptr, topLeft.x, topLeft.y, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
  } else {
    HWND insertAfter = OverlayZOrder::findInsertAfter(
        gw2, OverlayZOrder::kLayerRadial, m_hwnd);
    if (!insertAfter) {
      // No lower sibling — insert just above GW2
      insertAfter = GetNextWindow(gw2, GW_HWNDPREV);
    }
    // Cache check: skip SetWindowPos if insertion point unchanged.
    // Redundant SetWindowPos triggers DWM recomposition → flicker.
    if (insertAfter && insertAfter != m_hwnd &&
        insertAfter != m_lastInsertAfterHwnd) {
      SetWindowPos(m_hwnd, insertAfter, topLeft.x, topLeft.y, width, height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
      m_lastInsertAfterHwnd = insertAfter;
    } else if (!insertAfter || insertAfter == m_hwnd) {
      if (m_lastInsertAfterHwnd != HWND_TOP) {
        SetWindowPos(m_hwnd, HWND_TOP, topLeft.x, topLeft.y, width, height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        m_lastInsertAfterHwnd = HWND_TOP;
      } else {
        // Z-order unchanged — update position only
        SetWindowPos(m_hwnd, nullptr, topLeft.x, topLeft.y, width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
      }
    } else {
      // Z-order unchanged — update position only
      SetWindowPos(m_hwnd, nullptr, topLeft.x, topLeft.y, width, height,
                   SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
  }

  m_d3dContext.resize(QSize(width, height));
}

// ============================================================================
// GW2 Window Finding
// ============================================================================

namespace {
struct RadialEnumData {
  DWORD targetPid;
  HWND result;
  DWORD foundPid;
};

BOOL CALLBACK radialEnumWindowsProc(HWND hwnd, LPARAM lParam) {
  auto *data = reinterpret_cast<RadialEnumData *>(lParam);

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

  bool isGameWindow =
      (windowClass == QLatin1String("ArenaNet_Dx_Window_Class") ||
       windowClass == QLatin1String("ArenaNet_Gr_Window_Class"));

  if (isGameWindow) {
    data->result = hwnd;
    data->foundPid = windowPid;
    return FALSE;
  }

  return TRUE;
}
} // namespace

bool RadialOverlayWindow::findGW2WindowByPid(uint32_t pid) {
  RadialEnumData data = {};
  data.targetPid = static_cast<DWORD>(pid);
  data.result = nullptr;
  data.foundPid = 0;

  EnumWindows(radialEnumWindowsProc, reinterpret_cast<LPARAM>(&data));

  if (data.result) {
    m_gw2Hwnd = data.result;
    m_gw2ProcessId = data.foundPid;
    m_gw2ThreadId = GetWindowThreadProcessId(data.result, nullptr);
    qInfo() << "RadialOverlay: Found GW2 window — PID:" << data.foundPid;
    return true;
  }

  return false;
}

// ============================================================================
// WinEventHook — Event-Driven Position Tracking
// ============================================================================

void RadialOverlayWindow::installEventHook() {
  if (m_eventHook || m_gw2ThreadId == 0) {
    return;
  }

  m_eventHook =
      SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                      nullptr, RadialOverlayWindow::winEventProc,
                      m_gw2ProcessId, m_gw2ThreadId, WINEVENT_OUTOFCONTEXT);

  m_foregroundHook = SetWinEventHook(
      EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
      RadialOverlayWindow::foregroundProc, 0, 0, WINEVENT_OUTOFCONTEXT);

  if (m_eventHook) {
    s_hookMap.insert(m_eventHook, this);
  }
  if (m_foregroundHook) {
    s_hookMap.insert(m_foregroundHook, this);
  }

  qInfo() << "RadialOverlay: Event hooks installed";
}

void RadialOverlayWindow::uninstallEventHook() {
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

void CALLBACK RadialOverlayWindow::winEventProc(HWINEVENTHOOK hWinEventHook,
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

void CALLBACK RadialOverlayWindow::foregroundProc(
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
    self->m_contentVisible = true;
    if (self->m_hideOnUnfocus) {
      ShowWindow(self->m_hwnd, SW_SHOWNOACTIVATE);
    }
    QMetaObject::invokeMethod(
        self, [self]() { emit self->focusChanged(true); },
        Qt::QueuedConnection);
  } else {
    if (self->m_hideOnUnfocus) {
      ShowWindow(self->m_hwnd, SW_HIDE);
      self->m_contentVisible = false;
    } else {
      self->m_contentVisible = true;
    }
    QMetaObject::invokeMethod(
        self, [self]() { emit self->focusChanged(false); },
        Qt::QueuedConnection);
  }
}

// ============================================================================
// Render Loop
// ============================================================================

void RadialOverlayWindow::onMumbleDataUpdated() {
  if (!m_isTracking) {
    return;
  }

  // Deferred overlay creation: GW2 window wasn't found initially
  if (!m_gw2Hwnd && m_mumbleLink && m_mumbleLink->isConnected()) {
    uint32_t pid = m_targetPid ? m_targetPid : m_mumbleLink->processId();
    if (!tryCreateOverlay(pid)) {
      return; // Will retry on next MumbleLink signal
    }
  }

  if (!m_renderingEnabled) {
    return;
  }

  // Run idle callback (hotkey polling) even when wheel isn't rendering.
  // GetAsyncKeyState is trivially cheap and MUST run to detect activation.
  if (m_idleCallback) {
    m_idleCallback();
  }

  // Z-order enforcement: MUST run even when wheel is idle.
  // Per Z-Order.md: "Z-order MUST be enforced on every tick/frame."
  // Without this, after focus loss/regain the overlay window ends up
  // behind GW2 — the wheel activates (logs confirm) but is invisible
  // because DWM composites it behind the game window.
  // Throttled to ~15Hz to avoid excessive DWM recomposition.
  if (m_isTracking && m_d3dContext.isInitialized()) {
    if (++m_positionTickCount % 4 == 0) {
      updatePosition();
    }
  }

  // Skip entire render path when wheel is inactive.
  // RadialController sets m_wheelNeedsRendering=true on activation
  // and false on fade-out complete. Without this, onRenderFrame()
  // runs at 62.5Hz doing GPU work even when the wheel is hidden.
  if (!m_wheelNeedsRendering) {
    return;
  }

  onRenderFrame();
}

void RadialOverlayWindow::onRenderFrame() {
  if (!m_isTracking || !m_d3dContext.isInitialized()) {
    return;
  }

  // NOTE: No stall detection for radial. Unlike the 3D/minimap overlays
  // which need to auto-hide during loading screens, the radial is
  // user-activated and user-dismissed. Stall detection caused a death
  // spiral: radial GPU load → GW2 uiTick slows → stall detected →
  // radial hides → GPU freed → uiTick resumes → radial shows → repeat
  // at ~3Hz, causing 60-100 FPS drops.

  // Invoke the render callback — it returns true if it drew content.
  // If it returns false (wheel not active), skip beginFrame/Present
  // to avoid presenting 100 transparent frames/sec to the GPU.
  bool hasContent = false;
  if (m_renderCallback) {
    hasContent = m_renderCallback(&m_d3dContext);
  }

  if (hasContent) {
    m_d3dContext.endFrame();
  }
}

// ============================================================================
// Win32 Message Handler
// ============================================================================

LRESULT CALLBACK RadialOverlayWindow::windowProc(HWND hwnd, UINT msg,
                                                  WPARAM wParam,
                                                  LPARAM lParam) {
  auto *self = reinterpret_cast<RadialOverlayWindow *>(
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
    // Radial overlay is always click-through
    return HTTRANSPARENT;

  default:
    break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// =============================================================================
// Process Exit Wait
// =============================================================================

void RadialOverlayWindow::registerProcessExitWait() {
  unregisterProcessExitWait();

  if (m_gw2ProcessId == 0) {
    return;
  }

  m_gw2ProcessHandle = OpenProcess(SYNCHRONIZE, FALSE, m_gw2ProcessId);
  if (!m_gw2ProcessHandle) {
    qWarning() << "RadialOverlay: Failed to open GW2 process for exit wait,"
               << "PID:" << m_gw2ProcessId << "Error:" << GetLastError();
    return;
  }

  if (!RegisterWaitForSingleObject(&m_processWaitHandle, m_gw2ProcessHandle,
                                   processExitCallback, this, INFINITE,
                                   WT_EXECUTEONLYONCE)) {
    qWarning() << "RadialOverlay: RegisterWaitForSingleObject failed,"
               << "Error:" << GetLastError();
    CloseHandle(m_gw2ProcessHandle);
    m_gw2ProcessHandle = nullptr;
    return;
  }

  qInfo() << "RadialOverlay: Registered process exit wait for PID:"
          << m_gw2ProcessId;
}

void RadialOverlayWindow::unregisterProcessExitWait() {
  if (m_processWaitHandle) {
    UnregisterWaitEx(m_processWaitHandle, INVALID_HANDLE_VALUE);
    m_processWaitHandle = nullptr;
  }
  if (m_gw2ProcessHandle) {
    CloseHandle(m_gw2ProcessHandle);
    m_gw2ProcessHandle = nullptr;
  }
}

void CALLBACK RadialOverlayWindow::processExitCallback(PVOID context,
                                                       BOOLEAN timedOut) {
  Q_UNUSED(timedOut);
  auto *self = static_cast<RadialOverlayWindow *>(context);
  if (!self) {
    return;
  }

  QMetaObject::invokeMethod(self, "onGw2ProcessExited", Qt::QueuedConnection);
}

void RadialOverlayWindow::onGw2ProcessExited() {
  if (!m_isTracking) {
    return;
  }

  qInfo() << "RadialOverlay: GW2 process exited — hiding overlay";

  unregisterProcessExitWait();
  uninstallEventHook();

  if (m_hwnd) {
    ShowWindow(m_hwnd, SW_HIDE);
  }

  m_gw2Hwnd = nullptr;
  m_contentVisible = false;
  emit gameWindowFound(false);
}

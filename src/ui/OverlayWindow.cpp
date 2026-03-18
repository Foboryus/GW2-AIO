#include "OverlayWindow.h"
#include "ExclusionZoneEditor.h"
#include "OverlayMenuWidget.h"
#include "core/ThemeManager.h"
#include "features/markers/MarkerController.h"
#include "features/markers/MarkerSettingsManager.h"
#include "features/markers/MinimapRenderer.h"
#include <QDateTime>
#include <QDebug>
#include <QPainter>

#ifdef Q_OS_WIN
// Static instance for routing WinEventHook callbacks
OverlayWindow *OverlayWindow::s_instance = nullptr;
#endif

OverlayWindow::OverlayWindow(MumbleLink *mumble, QWidget *parent)
    : QWidget(parent,
              Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      m_mumbleLink(mumble) {
  // Enable transparency
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);

  // Create overlay menu widget (fills entire overlay)
  m_menuWidget = new OverlayMenuWidget(this);

  // Create exclusion zone editor (hidden by default)
  m_zoneEditor = new ExclusionZoneEditor(this);
  m_zoneEditor->setMumbleLink(m_mumbleLink);

  // Connect "Edit Custom Zones" button → open editor
  connect(m_menuWidget, &OverlayMenuWidget::editExclusionZonesRequested, this,
          [this]() {
            if (m_zoneEditor) {
              m_zoneEditor->setGeometry(0, 0, width(), height());
              m_zoneEditor->beginEditing();
            }
          });

  // Forward Details Tracker toggle to main.cpp → D3D11OverlayWindow
  connect(m_menuWidget, &OverlayMenuWidget::detailsTrackerToggled, this,
          &OverlayWindow::detailsTrackerToggled);

  // WA_TranslucentBackground makes transparent pixels pass clicks to GW2.
  // No manual click-through toggling needed — painted areas (icon, panel)
  // receive mouse events, transparent areas pass through naturally.

  // Connect MumbleLink signals
  connect(m_mumbleLink, &MumbleLink::connectionChanged, this,
          &OverlayWindow::onGameConnected);
  connect(m_mumbleLink, &MumbleLink::positionChanged, this,
          &OverlayWindow::onPositionChanged);
  connect(m_mumbleLink, &MumbleLink::dataUpdated, this,
          &OverlayWindow::updateHudVisibility);
  connect(m_mumbleLink, &MumbleLink::dataUpdated, this,
          &OverlayWindow::updateClickThrough);

#ifdef Q_OS_WIN
  s_instance = this;

  // Prevent overlay from stealing focus.
  // WS_EX_NOACTIVATE: mouse clicks never activate this window.
  // WS_EX_TRANSPARENT: entire window is click-through by default.
  // updateClickThrough() toggles WS_EX_TRANSPARENT off when the cursor
  // is over interactive areas (diamond icon, panel, zone editor).
  HWND overlayHwnd = reinterpret_cast<HWND>(winId());
  LONG exStyle = GetWindowLong(overlayHwnd, GWL_EXSTYLE);
  SetWindowLong(overlayHwnd, GWL_EXSTYLE,
                exStyle | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
#endif
}

OverlayWindow::~OverlayWindow() {
  unregisterProcessExitWait();
  uninstallEventHook();
#ifdef Q_OS_WIN
  if (s_instance == this) {
    s_instance = nullptr;
  }
#endif
}

void OverlayWindow::startTracking() {
  if (m_isTracking) {
    return;
  }

  m_isTracking = true;
  m_hwndLost = false;

  // Reset HUD visibility state for the new instance
  m_contentVisible = true;
  m_lastUiTick = 0;
  m_lastTickChangeMs = QDateTime::currentMSecsSinceEpoch();
  if (m_menuWidget) {
    m_menuWidget->setVisible(true);
  }

#ifdef Q_OS_WIN
  // Get GW2 PID from MumbleLink context (may be stale after instance switch)
  uint32_t pid = m_mumbleLink ? m_mumbleLink->processId() : 0;

  if (findGW2WindowByPid(pid)) {
    installEventHook();
    registerProcessExitWait();
    updatePosition();
    show();
    ensureZOrder();
    qInfo() << "Overlay tracking started (WinEventHook) PID:" << m_gw2ProcessId;
  } else if (pid != 0 && findGW2WindowByPid(0)) {
    // Stale PID from MumbleLink — fallback to finding any GW2 game window
    qInfo() << "Overlay: stale PID" << pid
            << "— found alternate GW2 window at PID:" << m_gw2ProcessId;
    installEventHook();
    registerProcessExitWait();
    updatePosition();
    show();
    ensureZOrder();
    qInfo() << "Overlay tracking started (WinEventHook) PID:" << m_gw2ProcessId;
  } else {
    qWarning() << "Overlay: cannot start tracking — GW2 HWND not found"
               << "(PID:" << pid << ")";
    m_isTracking = false;
  }
#endif
}

void OverlayWindow::stopTracking() {
  unregisterProcessExitWait();
  uninstallEventHook();
  m_gw2Hwnd = nullptr;
  m_isTracking = false;
  m_hwndLost = false;
#ifdef Q_OS_WIN
  m_gw2ThreadId = 0;
  m_gw2ProcessId = 0;
#endif
  hide();
}

void OverlayWindow::setClickThrough(bool enabled) {
#ifdef Q_OS_WIN
  HWND hwnd = reinterpret_cast<HWND>(winId());
  LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

  if (enabled) {
    exStyle |= WS_EX_TRANSPARENT | WS_EX_LAYERED;
  } else {
    exStyle &= ~WS_EX_TRANSPARENT;
  }

  SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
#else
  Q_UNUSED(enabled);
#endif
}

void OverlayWindow::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  // Overlay content is rendered by OverlayMenuWidget child widget.
  // OverlayWindow itself is just the transparent container.
  // No direct painting needed here — menu widget paints itself.
}

void OverlayWindow::updatePosition() {
#ifdef Q_OS_WIN
  if (!m_gw2Hwnd) {
    return;
  }

  HWND hwnd = static_cast<HWND>(m_gw2Hwnd);

  // Check if window still exists — GW2 may recreate its HWND during
  // splash→game transition or when AIO renames the window title.
  if (!IsWindow(hwnd)) {
    qInfo() << "Overlay: tracked HWND invalid, re-finding by PID:"
            << m_gw2ProcessId;
    m_gw2Hwnd = nullptr;
    uninstallEventHook();

    // Try to find new window immediately
    if (findGW2WindowByPid(m_gw2ProcessId)) {
      installEventHook();
      qInfo() << "Overlay: re-attached to new GW2 window";
      updatePosition();
    } else {
      // Game window not ready yet — set flag. MumbleLink's onPositionChanged
      // tick (50ms) will re-try on each pulse (trigger-based, no new timer).
      m_hwndLost = true;
      hide();
      qInfo() << "Overlay: HWND lost, waiting for MumbleLink tick to re-find";
    }
    return;
  }

  // Use client area (not window rect) — TacO/Blish overlay the game's
  // rendering area, not the title bar or window frame.
  RECT clientRect;
  if (GetClientRect(hwnd, &clientRect)) {
    POINT topLeft = {clientRect.left, clientRect.top};
    ClientToScreen(hwnd, &topLeft);
    int w = clientRect.right - clientRect.left;
    int h = clientRect.bottom - clientRect.top;
    setGeometry(topLeft.x, topLeft.y, w, h);
  }
#endif
}

bool OverlayWindow::findGW2WindowByPid(uint32_t pid) {
#ifdef Q_OS_WIN
  // pid=0 means MumbleLink context not yet populated — find ANY ArenaNet window
  struct EnumData {
    DWORD targetPid; // 0 = match any
    HWND bestHwnd;
    DWORD bestPid;
    DWORD threadId;
    bool foundGameWindow; // true = found actual game window (not splash)
  } data = {static_cast<DWORD>(pid), nullptr, 0, 0, false};

  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lParam);
        DWORD windowPid;
        DWORD threadId = GetWindowThreadProcessId(hwnd, &windowPid);

        // If targeting specific PID, skip non-matching windows
        if (d->targetPid != 0 && windowPid != d->targetPid) {
          return TRUE;
        }

        if (!IsWindowVisible(hwnd)) {
          return TRUE;
        }

        // Check window class — Blish HUD distinguishes:
        // "ArenaNet"                 = splash/patcher (REJECT)
        // "ArenaNet_Dx_Window_Class" = DX9 game window (ACCEPT)
        // "ArenaNet_Gr_Window_Class" = DX11 game window (ACCEPT)
        wchar_t className[256];
        GetClassNameW(hwnd, className, 256);
        QString windowClass = QString::fromWCharArray(className);

        // Only accept the actual game window classes, not the splash screen
        bool isGameWindow =
            (windowClass == QLatin1String("ArenaNet_Dx_Window_Class") ||
             windowClass == QLatin1String("ArenaNet_Gr_Window_Class"));

        if (isGameWindow) {
          d->bestHwnd = hwnd;
          d->bestPid = windowPid;
          d->threadId = threadId;
          d->foundGameWindow = true;
          return FALSE; // Found the game window — stop enumeration
        }

        return TRUE; // Continue searching
      },
      reinterpret_cast<LPARAM>(&data));

  if (data.bestHwnd && data.foundGameWindow) {
    m_gw2Hwnd = data.bestHwnd;
    m_gw2ThreadId = data.threadId;
    m_gw2ProcessId = data.bestPid;
    qInfo() << "Overlay: found GW2 game window — PID:" << data.bestPid
            << "Thread:" << data.threadId;
    return true;
  }

  qWarning() << "Overlay: no ArenaNet window found"
             << (pid > 0 ? QString("for PID: %1").arg(pid) : "(any)");
#else
  Q_UNUSED(pid);
#endif
  return false;
}

void OverlayWindow::installEventHook() {
#ifdef Q_OS_WIN
  uninstallEventHook(); // Clean up any previous hook

  if (!m_gw2Hwnd) {
    return;
  }

  // Hook: GW2 window location changes (move/resize)
  m_eventHook =
      SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, // eventMin
                      EVENT_OBJECT_LOCATIONCHANGE, // eventMax
                      nullptr,        // hmodWinEventProc (out-of-process)
                      winEventProc,   // lpfnWinEventProc
                      m_gw2ProcessId, // idProcess (filter to GW2)
                      m_gw2ThreadId,  // idThread (filter to GW2's UI thread)
                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  if (m_eventHook) {
    qInfo() << "Overlay: WinEventHook installed for PID:" << m_gw2ProcessId
            << "Thread:" << m_gw2ThreadId;
  } else {
    qWarning() << "Overlay: failed to install WinEventHook, error:"
               << GetLastError();
  }

  // Hook 2: Foreground changes (any process) — hide when GW2 loses focus
  m_foregroundHook = SetWinEventHook(
      EVENT_SYSTEM_FOREGROUND,       // eventMin
      EVENT_SYSTEM_FOREGROUND,       // eventMax
      nullptr, foregroundProc, 0, 0, // All processes, all threads
      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  if (m_foregroundHook) {
    qInfo() << "Overlay: foreground hook installed";
  }
#endif
}

void OverlayWindow::uninstallEventHook() {
#ifdef Q_OS_WIN
  if (m_eventHook) {
    UnhookWinEvent(m_eventHook);
    m_eventHook = nullptr;
  }
  if (m_foregroundHook) {
    UnhookWinEvent(m_foregroundHook);
    m_foregroundHook = nullptr;
  }
  qInfo() << "Overlay: hooks uninstalled";
#endif
}

#ifdef Q_OS_WIN
void CALLBACK OverlayWindow::winEventProc(HWINEVENTHOOK /*hWinEventHook*/,
                                          DWORD event, HWND hwnd, LONG idObject,
                                          LONG /*idChild*/,
                                          DWORD /*idEventThread*/,
                                          DWORD /*dwmsEventTime*/) {
  if (idObject != OBJID_WINDOW) {
    return;
  }

  if (!s_instance || !s_instance->m_gw2Hwnd) {
    return;
  }

  if (hwnd != static_cast<HWND>(s_instance->m_gw2Hwnd)) {
    return;
  }

  if (event == EVENT_OBJECT_LOCATIONCHANGE) {
    s_instance->updatePosition();
  }
}

void CALLBACK OverlayWindow::foregroundProc(HWINEVENTHOOK /*hWinEventHook*/,
                                            DWORD /*event*/, HWND hwnd,
                                            LONG /*idObject*/, LONG /*idChild*/,
                                            DWORD /*idEventThread*/,
                                            DWORD /*dwmsEventTime*/) {
  if (!s_instance || !s_instance->m_gw2Hwnd) {
    return;
  }

  HWND gw2Hwnd = static_cast<HWND>(s_instance->m_gw2Hwnd);
  HWND overlayHwnd = reinterpret_cast<HWND>(s_instance->winId());

  if (hwnd == gw2Hwnd) {
    // GW2 gained focus — overlay goes to TOPMOST
    SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Deferred re-apply: Windows may shuffle TOPMOST z-order after this
    // callback returns. Queue a second SetWindowPos on the next event loop
    // iteration (after Windows finishes its focus transition).
    QMetaObject::invokeMethod(
        s_instance,
        [overlayHwnd]() {
          SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        },
        Qt::QueuedConnection);
  } else {
    // GW2 lost focus — place overlay ABOVE GW2 but BELOW the focused app.
    // Blish HUD pattern: get the window just above GW2 in z-order,
    // then insert the overlay just below it.
    HWND nextHandle = GetWindow(gw2Hwnd, GW_HWNDPREV);
    if (nextHandle != nullptr && nextHandle != overlayHwnd) {
      SetWindowPos(overlayHwnd, nextHandle, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
}
#endif

void OverlayWindow::onGameConnected(bool connected) {
  if (connected) {
    qInfo() << "Overlay connected to GW2";
    // WA_TranslucentBackground + WS_EX_LAYERED handles click-through:
    // transparent pixels pass to GW2, painted areas receive mouse events.
    // Do NOT call setClickThrough(true) — that adds WS_EX_TRANSPARENT
    // which makes the ENTIRE window pass-through including the icon.
    startTracking();
  } else {
    qInfo() << "Overlay disconnected from GW2";
    stopTracking();
  }
  update();
}

// ============================================================================
// Click-Through Toggle (WS_EX_TRANSPARENT — TacO/Blish HUD pattern)
// ============================================================================

void OverlayWindow::updateClickThrough() {
#ifdef Q_OS_WIN
  // Determine if cursor is over an interactive area
  QPoint globalCursor = QCursor::pos();
  QPointF localCursor = mapFromGlobal(globalCursor);

  bool wantClickThrough = true; // Default: pass through to GW2

  // ExclusionZoneEditor active → entire window must be clickable
  if (m_zoneEditor && m_zoneEditor->isVisible()) {
    wantClickThrough = false;
  }

  // Diamond icon or open panel → clickable
  if (m_menuWidget &&
      m_menuWidget->isPointOverInteractiveArea(localCursor)) {
    wantClickThrough = false;
  }

  // Only toggle if state changed (avoid SetWindowLong on every tick)
  if (wantClickThrough != m_isClickThrough) {
    m_isClickThrough = wantClickThrough;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (m_isClickThrough) {
      SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
    } else {
      SetWindowLong(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
    }
  }
#endif
}

void OverlayWindow::onPositionChanged(float x, float y, float z) {
  m_playerX = x;
  m_playerY = y;
  m_playerZ = z;

#ifdef Q_OS_WIN
  // Deferred overlay start: if startTracking() failed at connectionChanged
  // time (GW2 window not yet available — common for Steam/Epic profiles),
  // retry on each MumbleLink tick. Mirrors
  // D3D11OverlayWindow::onMumbleDataUpdated().
  if (!m_isTracking && m_mumbleLink && m_mumbleLink->isConnected()) {
    startTracking();
  }

  // Trigger-based HWND re-find: when the game window was lost (e.g. during
  // splash→game transition), each MumbleLink tick tries to re-find it.
  // This mirrors Blish HUD's Update() pattern — no new timers needed.
  if (m_hwndLost && m_gw2ProcessId != 0) {
    if (findGW2WindowByPid(m_gw2ProcessId)) {
      m_hwndLost = false;
      installEventHook();
      updatePosition();
      show();
      ensureZOrder();
      qInfo() << "Overlay: re-attached to GW2 game window (MumbleLink tick)";
    }
    // If not found yet, keep m_hwndLost=true — next tick will retry
  }

  // Per-tick z-order refresh (TacO/Blish HUD pattern).
  // The foregroundProc hook is reactive — it only fires on focus changes.
  // But z-order can drift when other TOPMOST windows appear or Windows
  // reshuffles the z-order. Per-tick refresh ensures the overlay stays
  // visible above GW2 at all times.
  if (m_isTracking && m_gw2Hwnd) {
    ensureZOrder();
  }
#endif

  // Check HUD visibility based on game state
  updateHudVisibility();
}

void OverlayWindow::setMarkerController(MarkerController *controller) {
  if (m_menuWidget) {
    m_menuWidget->setMarkerController(controller);
  }

  // Reparent MinimapRenderer as child of this overlay window
  if (controller && controller->minimapRenderer()) {
    m_minimapRenderer = controller->minimapRenderer();
    m_minimapRenderer->setParent(this);
    m_minimapRenderer->setGeometry(0, 0, width(), height());
    m_minimapRenderer->lower(); // Draw below menu widget
    qInfo() << "OverlayWindow: MinimapRenderer reparented as child widget"
            << "size:" << width() << "x" << height();
  }
}

void OverlayWindow::setMarkerSettings(MarkerSettingsManager *settings) {
  m_markerSettings = settings;
  if (m_menuWidget) {
    m_menuWidget->setMarkerSettings(settings);
  }
  if (m_zoneEditor) {
    m_zoneEditor->setMarkerSettings(settings);
  }
}

void OverlayWindow::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // Keep child widgets sized to overlay
  if (m_menuWidget) {
    m_menuWidget->setGeometry(0, 0, width(), height());
  }
  if (m_zoneEditor && m_zoneEditor->isVisible()) {
    m_zoneEditor->setGeometry(0, 0, width(), height());
  }
  if (m_minimapRenderer) {
    m_minimapRenderer->setGeometry(0, 0, width(), height());
  }
}

void OverlayWindow::updateHudVisibility() {
  if (!m_mumbleLink || !m_mumbleLink->isConnected()) {
    return;
  }

  // TacO approach: track wall-clock time since uiTick last changed.
  // When GW2 is on a loading screen or character select, uiTick freezes.
  // After 333ms stale (TacO's proven threshold), hide the overlay.
  uint32_t currentTick = m_mumbleLink->uiTick();
  qint64 now = QDateTime::currentMSecsSinceEpoch();

  if (currentTick != m_lastUiTick) {
    m_lastUiTick = currentTick;
    m_lastTickChangeMs = now;
  }

  bool tickStalled = (now - m_lastTickChangeMs) >= kStallMs;

  // Hide when full map UI is open (isMapOpen = uiState bit 0x01)
  bool mapOpen = m_mumbleLink->isMapOpen();

  // showInBigMap: override mapOpen hide for the diamond icon + panel
  bool showInBigMap = m_menuWidget && m_menuWidget->markerSettings() &&
                      m_menuWidget->markerSettings()->showInBigMap();
  bool hideForMap = mapOpen && !showInBigMap;

  // Menu visibility: loading, char-select, map open → hide menu
  bool shouldShow = !tickStalled && !hideForMap;

  if (shouldShow != m_contentVisible) {
    m_contentVisible = shouldShow;
    if (m_menuWidget) {
      m_menuWidget->setShouldBeVisible(shouldShow);
    }
  }

  // Combat hide: only affects the panel, diamond stays visible
  if (m_menuWidget) {
    bool hideForCombat = m_mumbleLink->isInCombat() &&
                         m_menuWidget->markerSettings() &&
                         m_menuWidget->markerSettings()->hideInCombat();
    m_menuWidget->setCombatHidden(hideForCombat);
  }

  // Minimap auto-hide: stall (loading/char-select) triggers fade.
  // NOTE: mapOpen is NOT a hide condition — MinimapRenderer already
  // switches to big-map mode in paintEvent() (TacO approach).
  // Combat does NOT hide — instead MinimapRenderer draws a red border.
  if (m_minimapRenderer) {
    m_minimapRenderer->setShouldBeVisible(!tickStalled);
    m_minimapRenderer->setInCombat(m_mumbleLink->isInCombat());
  }
}

void OverlayWindow::ensureZOrder() {
#ifdef Q_OS_WIN
  if (!m_gw2Hwnd) {
    return;
  }

  HWND overlayHwnd = reinterpret_cast<HWND>(winId());
  HWND gw2Hwnd = static_cast<HWND>(m_gw2Hwnd);
  HWND foreground = ::GetForegroundWindow();

  if (foreground == gw2Hwnd || foreground == overlayHwnd) {
    // GW2 (or overlay) has focus — overlay goes TOPMOST
    HWND wnd = ::GetNextWindow(gw2Hwnd, GW_HWNDPREV);
    if (wnd != overlayHwnd) {
      ::SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  } else {
    // GW2 lost focus — place overlay just above GW2 (below focused app)
    HWND wnd = ::GetNextWindow(gw2Hwnd, GW_HWNDPREV);
    if (wnd && wnd != overlayHwnd) {
      ::SetWindowPos(overlayHwnd, wnd, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
#endif
}

// =============================================================================
// Process Exit Wait (RegisterWaitForSingleObject — instant GW2 exit detection)
// =============================================================================

#ifdef Q_OS_WIN
void OverlayWindow::registerProcessExitWait() {
  unregisterProcessExitWait(); // Clean up any previous wait

  if (m_gw2ProcessId == 0) {
    return;
  }

  m_gw2ProcessHandle = OpenProcess(SYNCHRONIZE, FALSE, m_gw2ProcessId);
  if (!m_gw2ProcessHandle) {
    qWarning() << "OverlayWindow: Failed to open GW2 process for exit wait,"
               << "PID:" << m_gw2ProcessId << "Error:" << GetLastError();
    return;
  }

  if (!RegisterWaitForSingleObject(&m_processWaitHandle, m_gw2ProcessHandle,
                                   processExitCallback, this, INFINITE,
                                   WT_EXECUTEONLYONCE)) {
    qWarning() << "OverlayWindow: RegisterWaitForSingleObject failed,"
               << "Error:" << GetLastError();
    CloseHandle(m_gw2ProcessHandle);
    m_gw2ProcessHandle = nullptr;
    return;
  }

  qInfo() << "OverlayWindow: Registered process exit wait for PID:"
          << m_gw2ProcessId;
}

void OverlayWindow::unregisterProcessExitWait() {
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

void CALLBACK OverlayWindow::processExitCallback(PVOID context,
                                                 BOOLEAN timedOut) {
  Q_UNUSED(timedOut);
  auto *self = static_cast<OverlayWindow *>(context);
  if (!self) {
    return;
  }

  // Marshal to main thread — callback runs on Windows thread pool
  QMetaObject::invokeMethod(self, "onGw2ProcessExited", Qt::QueuedConnection);
}
#else
void OverlayWindow::registerProcessExitWait() {}
void OverlayWindow::unregisterProcessExitWait() {}
void CALLBACK OverlayWindow::processExitCallback(PVOID, BOOLEAN) {}
#endif

void OverlayWindow::onGw2ProcessExited() {
  if (!m_isTracking) {
    return;
  }

  qInfo() << "OverlayWindow: GW2 process exited (instant detection)"
          << "— hiding overlay";
  stopTracking();
}

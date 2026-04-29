#include "OverlayWindow.h"
#include "ExclusionZoneEditor.h"
#include "OverlayMenuWidget.h"
#include "core/ThemeManager.h"
#include "core/OverlayZOrder.h"
#include "features/markers/MarkerSettingsManager.h"
#include "features/markers/MinimapRenderer.h"
#include <QDateTime>
#include <QDebug>
#include <QPainter>

#ifdef Q_OS_WIN
// REVIEW BEFORE BETA: s_hookMap is accessed only from Qt main thread (WinEventHook
// callbacks fire on the installing thread). Verify this holds with OverlayInstanceManager.
QHash<HWINEVENTHOOK, OverlayWindow *> OverlayWindow::s_hookMap;
#endif

OverlayWindow::OverlayWindow(MumbleLink *mumble, QWidget *parent, bool headless)
    : QWidget(parent,
              Qt::FramelessWindowHint | Qt::Tool),
      m_mumbleLink(mumble), m_headless(headless) {
  // Enable transparency
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);

  // Create overlay menu widget and zone editor (full mode only)
  if (!m_headless) {
    m_menuWidget = new OverlayMenuWidget(this);

    m_zoneEditor = new ExclusionZoneEditor(this);
    m_zoneEditor->setMumbleLink(m_mumbleLink);

    connect(m_menuWidget, &OverlayMenuWidget::editExclusionZonesRequested, this,
            [this]() {
              if (m_zoneEditor) {
                m_zoneEditor->setGeometry(0, 0, width(), height());
                m_zoneEditor->beginEditing();
              }
            });

    connect(m_menuWidget, &OverlayMenuWidget::detailsTrackerToggled, this,
            &OverlayWindow::detailsTrackerToggled);
  }

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
}

void OverlayWindow::setZOrderLayer(int layer) {
  m_zOrderLayer = layer;
  // Set window title for cross-process z-order discovery
  const wchar_t *suffix = (layer == OverlayZOrder::kLayerHUD) ? L"HUD" : L"Minimap";
  std::wstring title = OverlayZOrder::buildTitle(layer, suffix);
  setWindowTitle(QString::fromStdWString(title));
}

void OverlayWindow::startTracking() {
  if (m_isTracking) {
    return;
  }

  m_isTracking = true;
  m_hwndLost = false;

  // Start hidden — only show after uiTick confirms player is in-game.
  // Character select and loading screens have frozen uiTick, so the overlay
  // stays invisible until the player actually loads into a map.
  m_contentVisible = false;
  m_lastUiTick = 0;
  m_lastTickChangeMs = 0; // Epoch 0 → tickStalled immediately until uiTick changes
  if (m_menuWidget) {
    m_menuWidget->setVisible(true);          // QWidget must exist for paint
    m_menuWidget->setShouldBeVisible(false);  // Start with opacity 0
  }

#ifdef Q_OS_WIN
  // Use guaranteed command-line PID if set; fall back to MumbleLink PID.
  // MumbleLink processId() can contain stale data from a previous session's
  // shared memory, causing children to latch onto the WRONG GW2 window.
  uint32_t pid = m_targetPid;
  if (pid == 0) {
    pid = m_mumbleLink ? m_mumbleLink->processId() : 0;
    if (pid != 0) {
      qInfo() << "Overlay: using MumbleLink PID (no targetPid set):" << pid;
    }
  }

  if (findGW2WindowByPid(pid)) {
    installEventHook();
    registerProcessExitWait();
    updatePosition();
    show();
    ensureZOrder();
    // WinEventHook only fires on CHANGES — set initial focus state now.
    // Visibility is controlled ONLY by updateHudVisibility() — do NOT set
    // setShouldBeVisible(true) here. The overlay stays hidden until
    // mapId > 0 and position is valid (player actually in-game).
    HWND foreground = GetForegroundWindow();
    HWND gw2Hwnd = static_cast<HWND>(m_gw2Hwnd);
    if (foreground == gw2Hwnd) {
      setGameFocused(true);
    } else {
      setGameFocused(false);
    }

    qInfo() << "Overlay tracking started (WinEventHook) PID:" << m_gw2ProcessId
            << "(source:" << (m_targetPid ? "command-line" : "MumbleLink") << ")"
            << "gw2Focused:" << (foreground == gw2Hwnd);
  } else {
    // PID not found — wait for next MumbleLink poll tick.
    // DO NOT fall back to pid=0 (any ArenaNet window) — wrong for multibox.
    qWarning() << "Overlay: cannot start tracking — GW2 HWND not found"
               << "(PID:" << pid << ") — will retry on next MumbleLink tick";
    // Keep m_isTracking=true so the deferred retry in updateHudVisibility
    // can find the GW2 window on subsequent MumbleLink ticks.
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
    s_hookMap.insert(m_eventHook, this);
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
    s_hookMap.insert(m_foregroundHook, this);
    qInfo() << "Overlay: foreground hook installed";
  }
#endif
}

void OverlayWindow::uninstallEventHook() {
#ifdef Q_OS_WIN
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
  qInfo() << "Overlay: hooks uninstalled";
#endif
}

bool OverlayWindow::isAnyTrackedGW2Window(HWND hwnd) {
  if (!hwnd) {
    return false;
  }
  for (auto *instance : s_hookMap) {
    if (instance && instance->m_gw2Hwnd &&
        static_cast<HWND>(instance->m_gw2Hwnd) == hwnd) {
      return true;
    }
  }
  return false;
}

#ifdef Q_OS_WIN
void CALLBACK OverlayWindow::winEventProc(HWINEVENTHOOK hWinEventHook,
                                          DWORD event, HWND hwnd, LONG idObject,
                                          LONG /*idChild*/,
                                          DWORD /*idEventThread*/,
                                          DWORD /*dwmsEventTime*/) {
  if (idObject != OBJID_WINDOW) {
    return;
  }

  auto *self = s_hookMap.value(hWinEventHook, nullptr);
  if (!self || !self->m_gw2Hwnd) {
    return;
  }

  if (hwnd != static_cast<HWND>(self->m_gw2Hwnd)) {
    return;
  }

  if (event == EVENT_OBJECT_LOCATIONCHANGE) {
    self->updatePosition();
  }
}

void CALLBACK OverlayWindow::foregroundProc(HWINEVENTHOOK hWinEventHook,
                                            DWORD /*event*/, HWND hwnd,
                                            LONG /*idObject*/, LONG /*idChild*/,
                                            DWORD /*idEventThread*/,
                                            DWORD /*dwmsEventTime*/) {
  auto *self = s_hookMap.value(hWinEventHook, nullptr);
  if (!self || !self->m_gw2Hwnd) {
    return;
  }

  HWND gw2Hwnd = static_cast<HWND>(self->m_gw2Hwnd);
  HWND overlayHwnd = reinterpret_cast<HWND>(self->winId());

  if (hwnd == gw2Hwnd) {
    // GW2 gained focus — overlay shows content
    self->setGameFocused(true);

    if (self->m_zOrderLayer >= OverlayZOrder::kLayerHUD) {
      // HUD layer: TOPMOST to stay above all siblings
      SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

      // Deferred re-apply: Windows may shuffle TOPMOST z-order after this
      // callback returns. Queue a second SetWindowPos on the next event loop
      // iteration (after Windows finishes its focus transition).
      QMetaObject::invokeMethod(
          self,
          [overlayHwnd]() {
            SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
          },
          Qt::QueuedConnection);
    } else {
      // Non-HUD layer (e.g., minimap): use layer-based insertion
      self->ensureZOrder();
    }
    qInfo() << "[DIAG] OverlayWindow: FOREGROUND_GW2 layer:"
            << self->m_zOrderLayer;
  } else {
    // Another window gained focus — hide overlay content, show paused icon.
    self->setGameFocused(false);
    if (self->m_zOrderLayer >= OverlayZOrder::kLayerHUD) {
      // Clear TOPMOST so overlay stays behind the focused window.
      ::SetWindowPos(overlayHwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
}
#endif

void OverlayWindow::setGameFocused(bool focused) {
  if (m_gameFocused == focused) {
    return;
  }
  m_gameFocused = focused;
  qInfo() << "[DIAG] OverlayWindow: GAME_FOCUS_CHANGED" << "focused:" << focused;

  if (m_menuWidget) {
    // Close menu panel when losing focus (prevents interaction on wrong window)
    if (!focused && m_menuWidget->isMenuOpen()) {
      m_menuWidget->setMenuOpen(false);
    }
    m_menuWidget->setGameFocused(focused);
  } else if (m_headless) {
    // Headless mode (ChildMinimap): rendering throttle is handled by
    // ChildProcess::notifyOverlayFocusChanged via gameFocusChanged signal.
    // Don't hide/show the window here — in multibox, unfocused minimaps should
    // remain visible (showing the last rendered frame) so the user can
    // see markers on all instances.
  }

  // On focus gain: immediately update position, z-order, and force a
  // synchronous repaint. Without this, the overlay paints correctly but
  // stays invisible until onPositionChanged() fires (character movement).
  // updatePosition() refreshes window geometry (setGeometry → resizeEvent).
  // ensureZOrder() positions overlay above GW2 in z-order.
  // repaint() forces immediate synchronous paint + DWM surface flush
  // (update() only schedules deferred repaint, which DWM may not composite
  // immediately for WA_TranslucentBackground layered windows).
  if (focused && m_isTracking && m_gw2Hwnd) {
    updatePosition();
    ensureZOrder();
    qInfo() << "[DIAG] OverlayWindow: FOCUS_REATTACH"
            << "pos:" << geometry()
            << "gw2Hwnd:" << m_gw2Hwnd
            << "overlayHwnd:" << (void*)winId()
            << "layer:" << m_zOrderLayer
            << "visible:" << isVisible()
            << "headless:" << m_headless;
  }

  // Notify child processes for instant focus detection (Layer 2)
  emit gameFocusChanged(focused);

  if (focused) {
    repaint();  // Synchronous — forces DWM to recomposite NOW
    qInfo() << "[DIAG] OverlayWindow: FOCUS_REPAINT_DONE"
            << "widgetSize:" << size()
            << "isVisible:" << isVisible();
  } else {
    update();   // Async is fine for unfocus
  }
}

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

  // When GW2 is not focused, force full click-through — paused icon is
  // purely visual, no interaction. Prevents stealing clicks from the
  // focused GW2 window in multibox setups.
  if (!m_gameFocused) {
    // Already forced — skip interactive area checks
  } else {
    // ExclusionZoneEditor active → entire window must be clickable
    if (m_zoneEditor && m_zoneEditor->isVisible()) {
      wantClickThrough = false;
    }

    // Diamond icon or open panel → clickable
    if (m_menuWidget &&
        m_menuWidget->isPointOverInteractiveArea(localCursor)) {
      wantClickThrough = false;
    }
  }

  // Only toggle if state changed (avoid SetWindowLong on every tick)
  if (wantClickThrough != m_isClickThrough) {
    m_isClickThrough = wantClickThrough;
    qInfo() << "[DEVLOG] OverlayWindow: clickThrough changed to:" << m_isClickThrough;
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
  // Only when GW2 is focused — unfocused overlays stay behind the focused
  // window to prevent multibox tangling.
  if (m_isTracking && m_gw2Hwnd && m_gameFocused) {
    ensureZOrder();
  }
#endif

  // Check HUD visibility based on game state
  updateHudVisibility();
}

void OverlayWindow::setMarkerManager(MarkerManager *manager) {
  if (m_menuWidget) {
    m_menuWidget->setMarkerManager(manager);
  }

  // NOTE: MinimapRenderer reparenting removed — per-instance MinimapRenderer
  // is now created and reparented by OverlayInstance::start().
  // The shared MarkerController::minimapRenderer() is no longer used here.
}

void OverlayWindow::setMinimapRenderer(MinimapRenderer *renderer) {
  m_minimapRenderer = renderer;
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
  if (!m_mumbleLink) {
    return;
  }

  // Deferred GW2 window finding: if startTracking() was called before GW2
  // created its window (common in multibox — children spawn before game loads),
  // retry on each MumbleLink tick until the window is found.
  // This runs BEFORE the isConnected() check because GW2's window exists
  // long before MumbleLink connects (player must load into a map first).
  if (!m_gw2Hwnd && m_isTracking) {
    uint32_t pid = m_targetPid ? m_targetPid
                 : (m_mumbleLink->processId());
    if (pid != 0 && findGW2WindowByPid(pid)) {
      installEventHook();
      registerProcessExitWait();
      updatePosition();
      show();
      ensureZOrder();
      // Set initial focus state — visibility is controlled ONLY by
      // updateHudVisibility() below (map-based trigger).
      HWND foreground = GetForegroundWindow();
      HWND gw2Hwnd = static_cast<HWND>(m_gw2Hwnd);
      if (foreground == gw2Hwnd) {
        setGameFocused(true);
      } else {
        setGameFocused(false);
      }

      qInfo() << "Overlay: deferred tracking started — PID:" << m_gw2ProcessId;
    }
  }

  // [FOCUS_FIX] Throttled z-order enforcement (~10Hz instead of per-tick).
  // ensureZOrder() calls findInsertAfter() → EnumWindows() for non-HUD layers,
  // or SetWindowPos(HWND_TOPMOST) for HUD layers. Both are expensive Win32 calls
  // that trigger DWM recomposition. 10Hz is sufficient for z-order maintenance.
  // Modulo 4 at 62.5Hz focused poll = ~15.6Hz enforcement rate.
  if (m_isTracking && m_gw2Hwnd && m_gameFocused) {
    if (++m_zOrderTickCount % 4 == 0) {
      ensureZOrder();
    }
    // [DIAG] Throttled log: once per ~10 seconds
    if (m_zOrderTickCount % 625 == 1) {
      qInfo() << "[DIAG] OverlayWindow: ZORDER_TICK"
              << "layer:" << m_zOrderLayer
              << "overlayHwnd:" << (void*)winId()
              << "gw2Hwnd:" << m_gw2Hwnd
              << "visible:" << isVisible()
              << "enforcementHz: ~15";
    }
  }

  if (!m_mumbleLink->isConnected()) {
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

  // Map-based visibility: overlay only shows when player is in-game.
  // Character select (mapId=0), loading screens (position zeroed, uiTick stalled) → hidden.
  bool hasValidMap = m_mumbleLink->mapId() > 0;
  bool hasValidPosition = (m_mumbleLink->playerX() != 0.0f ||
                           m_mumbleLink->playerY() != 0.0f ||
                           m_mumbleLink->playerZ() != 0.0f);
  bool playerInGame = hasValidMap && hasValidPosition && !tickStalled;
  bool shouldShow = playerInGame && !hideForMap;

  if (shouldShow != m_contentVisible) {
    m_contentVisible = shouldShow;
    qInfo() << "[DIAG] OverlayWindow: HUD_VISIBILITY"
            << "visible:" << shouldShow
            << "mapId:" << m_mumbleLink->mapId()
            << "validPos:" << hasValidPosition
            << "tickStalled:" << tickStalled
            << "stallMs:" << (now - m_lastTickChangeMs)
            << "mapOpen:" << mapOpen
            << "hideForMap:" << hideForMap;
    if (m_menuWidget) {
      m_menuWidget->setShouldBeVisible(shouldShow);
    }
    // Propagate stall state to MinimapRenderer (loading/char select → hide).
    // Use !tickStalled (not shouldShow) because MinimapRenderer handles
    // both minimap AND big map rendering — hiding when map is open would
    // kill big map markers.
    if (m_minimapRenderer) {
      m_minimapRenderer->setShouldBeVisible(!tickStalled);
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

  if (m_zOrderLayer >= OverlayZOrder::kLayerHUD) {
    // HUD layer: per-tick TOPMOST maintenance. foregroundProc promotes to
    // TOPMOST on focus change, but Windows can shuffle TOPMOST z-order when
    // other TOPMOST windows appear. Only called when m_gameFocused is true.
    ::SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  } else {
    // Non-HUD layer (e.g., minimap): layer-based insertion.
    HWND gw2 = static_cast<HWND>(m_gw2Hwnd);
    HWND insertAfter = OverlayZOrder::findInsertAfter(
        gw2, m_zOrderLayer, overlayHwnd);
    if (!insertAfter) {
      insertAfter = ::GetNextWindow(gw2, GW_HWNDPREV);
    }
    // Cache check: skip SetWindowPos if insertion point unchanged.
    // Redundant SetWindowPos triggers DWM recomposition → flicker.
    if (insertAfter && insertAfter != overlayHwnd &&
        insertAfter != m_lastInsertAfterHwnd) {
      ::SetWindowPos(overlayHwnd, insertAfter, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      m_lastInsertAfterHwnd = insertAfter;
    } else if (!insertAfter || insertAfter == overlayHwnd) {
      if (m_lastInsertAfterHwnd != HWND_TOP) {
        ::SetWindowPos(overlayHwnd, HWND_TOP, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        m_lastInsertAfterHwnd = HWND_TOP;
      }
    }
    // else: z-order unchanged — no SetWindowPos needed
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

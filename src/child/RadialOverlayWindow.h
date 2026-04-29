#pragma once

/**
 * @file RadialOverlayWindow.h
 * @brief Lightweight D3D11 overlay window for the radial menu child process
 *
 * Adapted from D3D11OverlayWindow but stripped of all marker/trail dependencies.
 * Reuses the proven Win32/DComp/WinEventHook infrastructure for:
 * - Transparent click-through overlay via DirectComposition
 * - Event-driven GW2 window position tracking
 * - PID-based window targeting with deferred retry
 * - Focus-aware rendering (hide on unfocus)
 * - Process exit detection
 *
 * Rendering is driven by a pluggable callback set via setRenderCallback().
 * The radial controller provides its own draw logic through this callback.
 *
 * Consumers:
 * - ChildRadial: creates and owns this window
 * - RadialController: provides render callback
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialOverlayWindow.cpp)
 * - MarkerPipeline, TrailPipeline, SpriteBatch, GlyphAtlas, ExclusionZones
 * - Qt widget code (this is a pure Win32 window)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <dcomp.h>
#include <wrl/client.h>

#include <QHash>
#include <QObject>
#include <QSize>
#include <QElapsedTimer>

#include <functional>

#include "rendering/D3D11Context.h"

using Microsoft::WRL::ComPtr;

class MumbleLink;

class RadialOverlayWindow : public QObject {
  Q_OBJECT

public:
  explicit RadialOverlayWindow(MumbleLink *mumble, QObject *parent = nullptr);
  ~RadialOverlayWindow();

  // Non-copyable
  RadialOverlayWindow(const RadialOverlayWindow &) = delete;
  RadialOverlayWindow &operator=(const RadialOverlayWindow &) = delete;

  /**
   * @brief Start tracking GW2 window and rendering
   */
  void startTracking();

  /**
   * @brief Stop tracking and hide
   */
  void stopTracking();

  /**
   * @brief Set the guaranteed GW2 PID for HWND targeting.
   * Must be called before startTracking(). Uses the command-line PID
   * instead of MumbleLink processId() which may contain stale data.
   */
  void setTargetPid(uint32_t pid) { m_targetPid = pid; }

  /**
   * @brief Toggle hide-on-unfocus (TacO mode) vs always-visible (Blish mode)
   */
  void setHideOnUnfocus(bool hide) { m_hideOnUnfocus = hide; }

  /**
   * @brief Focus-aware throttling: pause/resume rendering
   */
  void setRenderingEnabled(bool enabled);

  /**
   * @brief Signal from RadialController that the wheel needs GPU rendering.
   *
   * When false, the entire onRenderFrame() path is skipped — no
   * updatePosition(), no stall detection, no GPU work. This eliminates
   * ~312 unnecessary onRenderFrame() calls/sec across 5 profiles.
   */
  void setWheelNeedsRendering(bool needs);

  /**
   * @brief Set the render callback — called each frame with the D3D11 context
   * Returns true if content was drawn (Present needed), false if idle.
   */
  void setRenderCallback(std::function<bool(D3D11Context *)> callback);

  /**
   * @brief Set the idle callback — called every MumbleLink tick even when
   * the wheel isn't rendering. Used for hotkey polling (GetAsyncKeyState)
   * which must run at all times to detect wheel activation.
   */
  void setIdleCallback(std::function<void()> callback);

  /**
   * @brief Get the D3D11 context for pipeline setup
   */
  D3D11Context *d3dContext() { return &m_d3dContext; }

  /**
   * @brief Check if overlay is active and rendering
   */
  bool isActive() const { return m_isTracking && m_hwnd != nullptr; }

  /**
   * @brief Get overlay window handle
   */
  HWND hwnd() const { return m_hwnd; }

signals:
  /**
   * @brief Emitted when GW2 window is found/lost
   */
  void gameWindowFound(bool found);

  /**
   * @brief Emitted when this instance's GW2 gains/loses foreground focus
   */
  void focusChanged(bool focused);

private slots:
  void onGameConnected(bool connected);
  void onMumbleDataUpdated();
  void onRenderFrame();
  void onGw2ProcessExited();

private:
  // --- Window management ---
  bool createOverlayWindow();
  void destroyOverlayWindow();
  bool setupDirectComposition();
  void updatePosition();
  bool findGW2WindowByPid(uint32_t pid);
  bool tryCreateOverlay(uint32_t pid);

  // --- WinEventHook for position tracking ---
  void installEventHook();
  void uninstallEventHook();

  static void CALLBACK winEventProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                    HWND hwnd, LONG idObject, LONG idChild,
                                    DWORD idEventThread, DWORD dwmsEventTime);
  static void CALLBACK foregroundProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                      HWND hwnd, LONG idObject, LONG idChild,
                                      DWORD idEventThread, DWORD dwmsEventTime);

  // --- Win32 message handling ---
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam);

  // --- Static hook-to-instance map for WinEventHook routing ---
  static QHash<HWINEVENTHOOK, RadialOverlayWindow *> s_hookMap;

  // Per-instance window class name (unique per instance)
  std::wstring m_windowClassName;

  // --- Core state ---
  MumbleLink *m_mumbleLink = nullptr;
  D3D11Context m_d3dContext;
  HWND m_hwnd = nullptr;
  HWND m_gw2Hwnd = nullptr;
  bool m_isTracking = false;
  bool m_contentVisible = true;
  bool m_hideOnUnfocus = false;
  uint32_t m_targetPid = 0;
  bool m_windowSearchLogged = false;
  bool m_renderingEnabled = false; // Must default false — matches ChildProcess::m_focused
  bool m_wheelNeedsRendering = false; // Set by RadialController: true=wheel active/fading

  // Pluggable render callback (set by RadialController)
  // Returns true if content was drawn (caller must call endFrame)
  std::function<bool(D3D11Context *)> m_renderCallback;

  // Lightweight idle callback — runs every tick even when wheel is hidden.
  // Used for hotkey polling (must run to detect wheel activation).
  std::function<void()> m_idleCallback;

  // DirectComposition (for per-pixel alpha compositing)
  ComPtr<IDCompositionDevice> m_dcompDevice;
  ComPtr<IDCompositionTarget> m_dcompTarget;
  ComPtr<IDCompositionVisual> m_dcompVisual;

  // Window event hooks
  HWINEVENTHOOK m_eventHook = nullptr;
  HWINEVENTHOOK m_foregroundHook = nullptr;
  DWORD m_gw2ThreadId = 0;
  DWORD m_gw2ProcessId = 0;

  // Process exit wait (RegisterWaitForSingleObject — instant detection)
  static void CALLBACK processExitCallback(PVOID context, BOOLEAN timedOut);
  void registerProcessExitWait();
  void unregisterProcessExitWait();
  HANDLE m_gw2ProcessHandle = nullptr;
  HANDLE m_processWaitHandle = nullptr;

  // HUD visibility (auto-hide on loading/char-select)
  uint32_t m_lastUiTick = 0;
  qint64 m_lastTickChangeMs = 0;
  static constexpr qint64 kStallMs = 333; // TacO threshold: 333ms stale → hide

  // Throttle counter for updatePosition() (contains EnumWindows — expensive)
  int m_positionTickCount = 0;
  bool m_needsClearFrame = false;

  // Z-order cache: skip SetWindowPos when insertion point unchanged.
  HWND m_lastInsertAfterHwnd = nullptr;

  // High-resolution frame timer
  QElapsedTimer m_frameTimer;

  // Static counter for unique window class names
  static int s_instanceCounter;
};

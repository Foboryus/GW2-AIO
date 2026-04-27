#pragma once

/**
 * @brief Native Win32 overlay window with D3D11 rendering
 *
 * Creates a borderless, transparent, click-through Win32 window that
 * tracks the GW2 game window position. Renders via D3D11Context.
 *
 * Transparency: Uses DirectComposition with CreateSwapChainForComposition
 * + DXGI_ALPHA_MODE_PREMULTIPLIED for per-pixel alpha compositing.
 * This is the only D3D11 method that supports transparent overlays
 * (DwmExtendFrameIntoClientArea does NOT work with flip-model swap chains).
 *
 * Rendering: Driven by MumbleLink::dataUpdated() signal (~50Hz when GW2
 * is running). No QTimer — purely event-driven for zero-latency data sync.
 *
 * Window tracking: Uses SetWinEventHook for event-driven position updates
 * (same pattern as existing OverlayWindow).
 *
 * Consumers:
 * - main.cpp: creates and owns this window
 * - MarkerPipeline, TrailPipeline, SpriteBatch: render into this window
 *
 * DO NOT ADD:
 * - Inline implementations (use D3D11OverlayWindow.cpp)
 * - Qt widget code (this is a pure Win32 window)
 * - Rendering pipeline logic (belongs in pipeline classes)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <dcomp.h>
#include <wrl/client.h>

#include <QHash>
#include <QObject>
#include <QSize>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector3D>

#include "D3D11Context.h"
#include "ExclusionData.h"

using Microsoft::WRL::ComPtr;

class MumbleLink;
class MarkerManager;
class MarkerSettingsManager;
class ImageCache;
class MarkerPipeline;
class TrailPipeline;
class SpriteBatch;
class GlyphAtlas;
class DebugOverlayWidget;
struct MarkerQueryContext;

class D3D11OverlayWindow : public QObject {
  Q_OBJECT

public:
  explicit D3D11OverlayWindow(MumbleLink *mumble, QObject *parent = nullptr);
  ~D3D11OverlayWindow();

  // Non-copyable
  D3D11OverlayWindow(const D3D11OverlayWindow &) = delete;
  D3D11OverlayWindow &operator=(const D3D11OverlayWindow &) = delete;

  /**
   * @brief Start tracking GW2 window and rendering
   */
  void startTracking();

  /**
   * @brief Stop tracking and hide
   */
  void stopTracking();

  /**
   * @brief Toggle click-through mode (input passes to game)
   */
  void setClickThrough(bool enabled);
  bool isClickThrough() const { return m_clickThrough; }

  /**
   * @brief Toggle hide-on-unfocus (TacO mode) vs always-visible (Blish mode)
   */
  void setHideOnUnfocus(bool hide) { m_hideOnUnfocus = hide; }

  /**
   * @brief Set the guaranteed GW2 PID for HWND targeting.
   * Must be called before startTracking(). Uses the command-line PID
   * instead of MumbleLink processId() which may contain stale data.
   */
  void setTargetPid(uint32_t pid) { m_targetPid = pid; }

  /**
   * @brief Get the D3D11 context for pipeline setup
   */
  D3D11Context *d3dContext() { return &m_d3dContext; }

  /**
   * @brief Wire marker data sources
   */
  void setMarkerManager(MarkerManager *manager);
  void setMarkerSettings(MarkerSettingsManager *settings);
  void setImageCache(ImageCache *cache);

  /// Per-instance query context (Phase 7a) — propagated to pipelines
  void setQueryContext(const MarkerQueryContext *ctx);

  /**
   * @brief Get the overlay HWND
   */
  HWND hwnd() const { return m_hwnd; }

  /**
   * @brief Check if overlay is active and rendering
   */
  bool isActive() const { return m_isTracking && m_hwnd != nullptr; }

  /**
   * @brief Show/hide the Details Tracker overlay (user-toggled)
   */
  void toggleDebugOverlay(bool visible);

  /**
   * @brief Set the paired Qt overlay HWND for z-order coordination
   * Each D3D11 overlay positions itself below its paired Qt overlay.
   */
  void setQtOverlayHwnd(HWND hwnd) { m_qtOverlayHwnd = hwnd; }

  /**
   * @brief Focus-aware throttling: pause/resume D3D11 rendering
   * Set by OverlayInstance when GW2 focus changes.
   */
  void setRenderingEnabled(bool enabled);

signals:
  /**
   * @brief Emitted when GW2 window is found/lost
   */
  void gameWindowFound(bool found);

  /**
   * @brief Emitted when overlay menu opens/closes (for click-through toggle)
   */
  void menuToggled(bool open);

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

  // --- Rendering ---
  void render();

  // --- Exclusion zones ---
  bool createExclusionBuffer();
  void updateAndBindExclusionZones();

  // --- Win32 message handling ---
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam);

  // --- Static hook-to-instance map for WinEventHook routing ---
  // Maps each HWINEVENTHOOK handle to its owning instance.
  // Supports N overlay instances in the same process.
  static QHash<HWINEVENTHOOK, D3D11OverlayWindow *> s_hookMap;

  /**
   * @brief Check if an HWND belongs to any tracked GW2 instance
   * Used by foreground/z-order logic to stay TOPMOST when a sibling
   * GW2 instance has focus (multibox friendly).
   */
  static bool isAnyTrackedGW2Window(HWND hwnd);

  // Per-instance window class name (unique per instance)
  std::wstring m_windowClassName;

  // Qt overlay HWND for z-order coordination (set via setQtOverlayHwnd)
  HWND m_qtOverlayHwnd = nullptr;

  // --- Core state ---
  MumbleLink *m_mumbleLink = nullptr;
  MarkerManager *m_markerManager = nullptr;
  MarkerSettingsManager *m_markerSettings = nullptr;

  D3D11Context m_d3dContext;
  HWND m_hwnd = nullptr;
  HWND m_gw2Hwnd = nullptr;
  bool m_isTracking = false;
  bool m_clickThrough = true;
  bool m_contentVisible = true;
  bool m_hideOnUnfocus = false; // false = Blish mode (always visible)
  uint32_t m_targetPid = 0;     // Guaranteed GW2 PID from command-line
  bool m_windowSearchLogged = false; // Log-once guard for tryCreateOverlay warning
  bool m_renderingEnabled = true;    // Focus-aware throttle — false skips rendering

  // Data-driven rendering only (TacO/Blish pattern):
  // Renders solely on MumbleLink::dataUpdated signal (~50Hz).
  // No gap-filling timer — every rendered frame uses fresh data.

  // Rendering pipelines (owned)
  MarkerPipeline *m_markerPipeline = nullptr;
  TrailPipeline *m_trailPipeline = nullptr;
  SpriteBatch *m_spriteBatch = nullptr;
  GlyphAtlas *m_glyphAtlas = nullptr;
  ImageCache *m_imageCache = nullptr;

  // Per-instance query context (Phase 7a) — not owned, stored for deferred propagation
  const MarkerQueryContext *m_queryCtx = nullptr;

  // Debug coordinate overlay (diagnostic)
  DebugOverlayWidget *m_debugOverlay = nullptr;

  // DirectComposition (for per-pixel alpha compositing)
  ComPtr<IDCompositionDevice> m_dcompDevice;
  ComPtr<IDCompositionTarget> m_dcompTarget;
  ComPtr<IDCompositionVisual> m_dcompVisual;

  // Exclusion zone constant buffer (shared b2 for both pipelines)
  ComPtr<ID3D11Buffer> m_exclusionCB;

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

  // HUD visibility (auto-hide on loading/char-select — TacO approach)
  uint32_t m_lastUiTick = 0;
  qint64 m_lastTickChangeMs = 0; // Elapsed ms when uiTick last changed
  static constexpr qint64 kStallMs = 200; // Hide after 200ms stale (TacO: 333ms)
  bool m_needsClearFrame = false; // Push one clear frame on hide transition

  // High-resolution frame timer (for stall detection)
  // Uses QueryPerformanceCounter on Windows — zero allocation per call.
  QElapsedTimer m_frameTimer;

  // Static counter for unique window class names
  static int s_instanceCounter;
};

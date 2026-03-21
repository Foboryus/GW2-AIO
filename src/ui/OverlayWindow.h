#pragma once

#include <QHash>
#include <QWidget>

#include "core/MumbleLink.h"

#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
// clang-format on
#endif

class MarkerController;
class MarkerSettingsManager;
class MinimapRenderer;
class OverlayMenuWidget;
class ExclusionZoneEditor;

/**
 * @brief Transparent overlay window for in-game HUD
 *
 * Window tracking uses SetWinEventHook (EVENT_OBJECT_LOCATIONCHANGE)
 * and foreground detection (EVENT_SYSTEM_FOREGROUND) — event-driven.
 */
class OverlayWindow : public QWidget {
  Q_OBJECT

public:
  explicit OverlayWindow(MumbleLink *mumble, QWidget *parent = nullptr);
  ~OverlayWindow();

  /**
   * @brief Start tracking GW2 window position (event-based via WinEventHook)
   */
  void startTracking();

  /**
   * @brief Stop tracking and hide
   */
  void stopTracking();

  /**
   * @brief Set click-through mode (input passes to game)
   */
  void setClickThrough(bool enabled);

signals:
  void editExclusionZonesRequested();
  void detailsTrackerToggled(bool visible);

public:
  /**
   * @brief Wire marker data sources for the overlay menu
   */
  void setMarkerController(MarkerController *controller);
  void setMarkerSettings(MarkerSettingsManager *settings);

  /**
   * @brief Set the per-instance MinimapRenderer for resize tracking
   * Called by OverlayInstance after reparenting the renderer.
   */
  void setMinimapRenderer(MinimapRenderer *renderer);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void updatePosition();
  void onGameConnected(bool connected);
  void onPositionChanged(float x, float y, float z);
  void onGw2ProcessExited();

private:
  /**
   * @brief Find GW2 window by MumbleLink processId using EnumWindows
   * Searches for ArenaNet_Gr_Window_Class (game window, not splash)
   */
  bool findGW2WindowByPid(uint32_t pid);

  /**
   * @brief Install/uninstall WinEventHook for window position tracking
   */
  void installEventHook();
  void uninstallEventHook();

#ifdef Q_OS_WIN
  /**
   * @brief WinEventHook callback — fired by Windows when GW2
   * moves/resizes/closes
   */
  static void CALLBACK winEventProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                    HWND hwnd, LONG idObject, LONG idChild,
                                    DWORD idEventThread, DWORD dwmsEventTime);

  /**
   * @brief Foreground hook callback — fired when any window gains focus
   */
  static void CALLBACK foregroundProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                      HWND hwnd, LONG idObject, LONG idChild,
                                      DWORD idEventThread, DWORD dwmsEventTime);

  // Static hook-to-instance map for WinEventHook routing
  // Maps each HWINEVENTHOOK handle to its owning instance.
  // Supports N overlay instances in the same process.
  static QHash<HWINEVENTHOOK, OverlayWindow *> s_hookMap;

  /**
   * @brief Check if an HWND belongs to any tracked GW2 instance
   * Used by foreground/z-order logic to stay TOPMOST when a sibling
   * GW2 instance has focus (multibox friendly).
   */
  static bool isAnyTrackedGW2Window(HWND hwnd);

private:

  // Process exit wait (RegisterWaitForSingleObject — instant detection)
  static void CALLBACK processExitCallback(PVOID context, BOOLEAN timedOut);
  void registerProcessExitWait();
  void unregisterProcessExitWait();
  HANDLE m_gw2ProcessHandle = nullptr;
  HANDLE m_processWaitHandle = nullptr;

  HWINEVENTHOOK m_eventHook = nullptr;
  HWINEVENTHOOK m_foregroundHook = nullptr;
  DWORD m_gw2ThreadId = 0;
  DWORD m_gw2ProcessId = 0;
#endif

  MumbleLink *m_mumbleLink;

  // GW2 window tracking
  void *m_gw2Hwnd = nullptr;
  bool m_isTracking = false;
  bool m_hwndLost = false; // True when HWND went invalid, re-find pending

  // Player data for HUD
  float m_playerX = 0;
  float m_playerY = 0;
  float m_playerZ = 0;

  // Dynamic HUD state (auto-hide on loading/char-select — TacO approach)
  bool m_contentVisible = true;  // Whether overlay content is drawn
  uint32_t m_lastUiTick = 0;     // Last seen uiTick value
  qint64 m_lastTickChangeMs = 0; // Wall-clock ms when uiTick last changed
  static constexpr qint64 kStallMs = 333; // TacO threshold: 333ms stale → hide

  void updateHudVisibility();

  /**
   * @brief Toggle WS_EX_TRANSPARENT based on cursor over interactive areas
   * Called at ~50Hz from MumbleLink::dataUpdated. Blish HUD/TacO pattern.
   */
  void updateClickThrough();

  /**
   * @brief Ensure overlay z-order is above GW2 window
   * TacO/Blish pattern: if GW2 has focus, overlay is TOPMOST;
   * otherwise, overlay sits just above GW2 in z-order.
   */
  void ensureZOrder();

  // Overlay menu
  OverlayMenuWidget *m_menuWidget = nullptr;
  ExclusionZoneEditor *m_zoneEditor = nullptr;

  // Minimap marker renderer (child widget, fills entire overlay)
  MinimapRenderer *m_minimapRenderer = nullptr;

  // Marker settings (for minimap auto-hide: combat, toggles)
  MarkerSettingsManager *m_markerSettings = nullptr;

  // Click-through state (WS_EX_TRANSPARENT toggle tracking)
  bool m_isClickThrough = true; // Start transparent — game gets all clicks
};

#pragma once

/**
 * @file RadialController.h
 * @brief Central controller for radial menu rendering in the ChildRadial process
 *
 * Manages RadialOverlayWindow, RadialRenderer, and RadialWheel.
 * Detects hotkey press-and-hold (X key) via GetAsyncKeyState polling,
 * shows/hides the radial wheel, and handles element selection on release.
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialController.cpp)
 * - UI code (belongs in RadialTabWidget)
 * - Marker/trail logic (belongs in marker pipelines)
 */

#include <QObject>
#include <QElapsedTimer>

#include "core/RadialSettings.h"

class D3D11Context;
class MumbleLink;
class RadialOverlayWindow;
class RadialRenderer;
class RadialWheel;

class RadialController : public QObject {
  Q_OBJECT

public:
  explicit RadialController(MumbleLink *mumble, uint32_t targetPid,
                            QObject *parent = nullptr);
  ~RadialController();

  /**
   * @brief Start the overlay (creates window, begins tracking)
   */
  void start();

  /**
   * @brief Stop the overlay (destroys window, stops tracking)
   */
  void stop();

  /**
   * @brief Handle settings received from IPC
   */
  void onSettingsReceived(const QJsonObject &settings);

  /**
   * @brief Apply radial settings (from disk or IPC)
   *
   * Replaces all hardcoded configuration with per-profile values.
   * Rebuilds wheel elements, updates hotkey VK, display params.
   */
  void applySettings(const RadialSettings &settings);

  /**
   * @brief Handle focus change from parent process
   */
  void onFocusChanged(bool focused);

  /**
   * @brief Get the overlay window (for connecting focusChanged signal)
   */
  RadialOverlayWindow *overlayWindow() const { return m_overlayWindow; }

private:
  /**
   * @brief Render callback — called by RadialOverlayWindow each frame
   * @return true if content was drawn (needs Present), false if idle
   */
  bool renderFrame(D3D11Context *ctx);

  /**
   * @brief Poll hotkey state and manage wheel lifecycle
   */
  void pollHotkey();

  /**
   * @brief Populate mount elements from m_settings (replaces setupTestElements)
   */
  void rebuildElements();

  /**
   * @brief Load icon textures for all elements (called after renderer init)
   */
  void loadIconTextures(D3D11Context *ctx);

  MumbleLink *m_mumbleLink = nullptr;
  uint32_t m_targetPid = 0;
  RadialOverlayWindow *m_overlayWindow = nullptr;
  RadialRenderer *m_renderer = nullptr;
  RadialWheel *m_wheel = nullptr;
  // Per-profile settings (from RadialSettingsManager)
  RadialSettings m_settings;

  // Hotkey state
  int m_triggerVK = 0;        // From m_settings.mountHotkey (0 = disabled)
  int m_triggerModifiers = 0; // Qt::KeyboardModifiers flags for the trigger
  bool m_wasKeyDown = false;
  bool m_isFocused = false; // Must match ChildProcess::m_focused default
  bool m_iconsLoaded = false;

  // Frame timing
  QElapsedTimer m_frameTimer;
  qint64 m_lastFrameMs = 0;

  // Fade-out state (Phase 1 fix: clear swap chain on deactivation)
  bool m_wasWheelActive = false;     // Tracks active→inactive transition
  float m_fadeAlpha = 0.0f;          // 1.0→0.0 during fade-out
  float m_savedGlobalOpacity = 1.0f; // Restore after fade
};

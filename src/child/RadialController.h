#pragma once

/**
 * @file RadialController.h
 * @brief Central controller for radial menu rendering in the ChildRadial process
 *
 * Manages RadialOverlayWindow, RadialRenderer, and three independent RadialWheels
 * (mount, novelty, marker), each with its own hotkey.
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialController.cpp)
 * - UI code (belongs in RadialTabWidget)
 */

#include <QObject>
#include <QElapsedTimer>

#include "core/RadialSettings.h"

class D3D11Context;
class MumbleLink;
class RadialOverlayWindow;
class RadialRenderer;
class RadialWheel;

/**
 * @brief Per-wheel hotkey and state tracking
 */
struct WheelState {
  RadialWheel *wheel = nullptr;
  int triggerVK = 0;          // Virtual key code for this wheel's hotkey
  int triggerModifiers = 0;   // GW2 bitmask: 1=Shift, 2=Ctrl, 4=Alt
  bool wasKeyDown = false;    // Previous frame key state
  bool noHoldOpen = false;    // Tracks wheel open state in no-hold mode
  QString wheelType;          // "mount", "novelty", or "marker"
};

class RadialController : public QObject {
  Q_OBJECT

public:
  explicit RadialController(MumbleLink *mumble, uint32_t targetPid,
                            QObject *parent = nullptr);
  ~RadialController();

  void start();
  void stop();
  void onSettingsReceived(const QJsonObject &settings);
  void applySettings(const RadialSettings &settings);
  void onFocusChanged(bool focused);

  RadialOverlayWindow *overlayWindow() const { return m_overlayWindow; }

  // --- Headless mode (SharedTexture) ---
  void setD3DContext(D3D11Context *ctx) { m_externalCtx = ctx; }
  void invalidateGPUResources();
  void startHeadless();
  bool needsRendering() const;
  void renderToTarget(D3D11Context *ctx, int cursorX, int cursorY,
                      int viewW, int viewH);
  void pollHotkey();
  void setLoadingScreen(bool loading);

private:
  bool renderFrame(D3D11Context *ctx);
  void rebuildElements();
  void loadIconTextures(D3D11Context *ctx);

  /**
   * @brief Check one wheel's hotkey and manage its lifecycle
   * @return true if this wheel consumed a key event (blocks other wheels)
   */
  bool pollWheelHotkey(WheelState &ws);

  /**
   * @brief Handle element selection when a wheel is deactivated
   */
  void handleSelection(const QString &selectedId, const WheelState &ws);

  /**
   * @brief Deactivate whichever wheel is currently active
   */
  void deactivateActiveWheel();

  /**
   * @brief Build elements for a specific wheel type
   */
  void buildMountElements(RadialWheel *wheel, bool usePng);
  void buildNoveltyElements(RadialWheel *wheel, bool usePng);
  void buildMarkerElements(RadialWheel *wheel, bool usePng);

  /**
   * @brief Look up keybind for selected element in the right settings map
   */
  const RadialElementConfig* findElementConfig(const QString &settingsKey,
                                                const WheelState &ws) const;

  MumbleLink *m_mumbleLink = nullptr;
  uint32_t m_targetPid = 0;
  RadialOverlayWindow *m_overlayWindow = nullptr;
  RadialRenderer *m_renderer = nullptr;
  RadialSettings m_settings;

  // Three independent wheels
  WheelState m_mountState;
  WheelState m_noveltyState;
  WheelState m_markerState;

  // Points to whichever wheel is currently active/fading (or nullptr)
  RadialWheel *m_activeWheel = nullptr;
  WheelState *m_activeWheelState = nullptr;

  bool m_isFocused = false;
  bool m_iconsLoaded = false;

  // Frame timing
  QElapsedTimer m_frameTimer;
  qint64 m_lastFrameMs = 0;

  // External D3D11 context for headless mode (not owned)
  D3D11Context *m_externalCtx = nullptr;
  bool m_headless = false;

  // Fade-out state
  bool m_wasWheelActive = false;
  float m_fadeAlpha = 0.0f;
  float m_savedGlobalOpacity = 1.0f;

  // Loading screen state — blocks hotkey activation
  bool m_loadingScreen = false;

  // Fast Mount Swap — dismount-on-open approach
  // Sequence: [mount CD 600ms] → dismount → [dismount CD 400ms] → new mount
  bool m_fastSwapDismountSent = false;     // True if we auto-dismounted on open
  QElapsedTimer m_fastSwapDismountTimer;   // When the dismount was sent
  QElapsedTimer m_lastMountSentTimer;      // When the last mount keybind was sent
  bool m_lastMountSentValid = false;       // True after first mount sent
  static constexpr int kMountCooldownMs = 600;      // CD after mount before dismount works
  static constexpr int kDismountCooldownMs = 400;    // CD after dismount before mount works
};

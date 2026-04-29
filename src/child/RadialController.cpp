/**
 * @file RadialController.cpp
 * @brief Central controller for radial menu rendering
 *
 * Phase 2A: Settings-driven radial wheel with:
 * - RadialRenderer (shader-based wheel, element, cursor rendering)
 * - RadialWheel (state management, hover detection, animation)
 * - Hotkey detection via GetAsyncKeyState polling (configurable VK)
 * - Mount elements built from per-profile RadialSettings
 */

// clang-format off
#include <windows.h>
// clang-format on

#include "RadialController.h"

#include <QDebug>
#include <QJsonObject>

#include "RadialOverlayWindow.h"
#include "RadialRenderer.h"
#include "RadialWheel.h"
#include "rendering/D3D11Context.h"
#include "core/MumbleLink.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

RadialController::RadialController(MumbleLink *mumble, uint32_t targetPid,
                                   QObject *parent)
    : QObject(parent), m_mumbleLink(mumble), m_targetPid(targetPid) {
  m_renderer = new RadialRenderer();
  m_wheel = new RadialWheel();

  // Elements are NOT populated here — wait for applySettings() from ChildRadial.
  // This avoids hardcoding any default elements.
}

RadialController::~RadialController() {
  stop();
  delete m_renderer;
  delete m_wheel;
}

// ============================================================================
// Lifecycle
// ============================================================================

void RadialController::start() {
  if (m_overlayWindow) {
    return; // Already started
  }

  m_overlayWindow = new RadialOverlayWindow(m_mumbleLink, this);
  m_overlayWindow->setTargetPid(m_targetPid);
  m_overlayWindow->setHideOnUnfocus(false); // Always visible (Blish mode)

  // Set the render callback — invoked each frame by the overlay window.
  // Returns true when content was drawn (needs Present), false when idle.
  m_overlayWindow->setRenderCallback(
      [this](D3D11Context *ctx) -> bool { return renderFrame(ctx); });

  // Set the idle callback — runs every tick even when wheel is hidden.
  // Polls hotkeys (GetAsyncKeyState) to detect wheel activation.
  m_overlayWindow->setIdleCallback([this]() { pollHotkey(); });

  m_frameTimer.start();
  m_lastFrameMs = m_frameTimer.elapsed();

  m_overlayWindow->startTracking();
  m_isRunning = true;
  qInfo() << "RadialController: Started for PID:" << m_targetPid;
}

void RadialController::stop() {
  if (!m_isRunning) return;  // Guard against double-stop
  m_isRunning = false;

  if (m_overlayWindow) {
    m_overlayWindow->stopTracking();
    delete m_overlayWindow;
    m_overlayWindow = nullptr;
  }

  m_renderer->shutdown();
  qInfo() << "RadialController: Stopped";
}

void RadialController::onSettingsReceived(const QJsonObject &settings) {
  // IPC settings are now parsed by ChildRadial, which calls applySettings()
  // directly. This method is kept for interface compliance but should not
  // be called directly.
  Q_UNUSED(settings);
  qWarning() << "RadialController::onSettingsReceived called directly —"
             << "use applySettings() instead";
}

void RadialController::applySettings(const RadialSettings &settings) {
  m_settings = settings;

  // Update hotkey from settings
  m_triggerVK = m_settings.mountHotkey;
  m_triggerModifiers = m_settings.mountHotkeyModifiers;
  qInfo() << "RadialController: Settings applied — mountHotkey VK:"
          << m_triggerVK
          << "modifiers:" << m_triggerModifiers
          << "wheelScale:" << m_settings.wheelScale
          << "opacity:" << m_settings.opacity
          << "enabled mounts:" << m_settings.mounts.size();

  // Apply display settings to wheel
  m_wheel->setCenterScale(m_settings.centerScale);
  m_wheel->setGlobalOpacity(m_settings.opacity);
  if (m_settings.animationTimeMs > 0) {
    // Convert animation time to speed multiplier
    // Default is 150ms, kWheelFadeSpeed=4.0 → scale proportionally
    float speedMultiplier = 150.0f / static_cast<float>(m_settings.animationTimeMs);
    m_wheel->setAnimationSpeed(speedMultiplier);
  }

  // Apply wheel scale to renderer (base 0.72 * user scale factor)
  // wheelScale=1.0 → 0.72 (default), wheelScale=1.5 → 1.08 (larger)
  m_renderer->setWheelScale(0.72f * m_settings.wheelScale);

  // Rebuild elements from settings
  rebuildElements();

  // Force icon reload on next render frame
  m_iconsLoaded = false;
}

void RadialController::onFocusChanged(bool focused) {
  m_isFocused = focused;

  qInfo() << "[DIAG] RadialController: FOCUS_CHANGED"
          << "focused:" << focused
          << "wheelActive:" << m_wheel->isActive()
          << "triggerVK:" << m_triggerVK;

  // Focus loss while wheel is active or fading:
  // Present a clear frame BEFORE disabling rendering to flush swap chain.
  if (!focused && m_overlayWindow) {
    bool needsClear = m_wheel->isActive() || m_fadeAlpha > 0.0f;

    // Deactivate wheel logically
    if (m_wheel->isActive()) {
      m_wheel->deactivate();
      m_wasKeyDown = false;
      qInfo() << "[DIAG] RadialController: WHEEL_DEACTIVATED (focus lost)";
    }

    // Cancel any in-progress fade and restore opacity
    if (m_fadeAlpha > 0.0f) {
      m_wheel->setGlobalOpacity(m_savedGlobalOpacity);
      m_fadeAlpha = 0.0f;
      m_wasWheelActive = false;
    }

    // Present one transparent frame to clear the swap chain
    if (needsClear) {
      auto *ctx = m_overlayWindow->d3dContext();
      if (ctx && ctx->isInitialized()) {
        ctx->beginFrame();  // Clears to transparent black
        ctx->endFrame();    // Present → DWM composites transparent
        qInfo() << "[DIAG] RadialController: FOCUS_LOSS_CLEAR_FRAME presented";
      }
    }

    m_overlayWindow->setWheelNeedsRendering(false);
    m_overlayWindow->setRenderingEnabled(false);
  } else if (focused && m_overlayWindow) {
    m_overlayWindow->setRenderingEnabled(true);
  }
}

// ============================================================================
// Element Rebuilding (from RadialSettings)
// ============================================================================

// Static mapping: settings key → SVG icon filename
static const QMap<QString, QString> kMountIconMap = {
    {"raptor",    "mount_raptor.svg"},
    {"springer",  "mount_springer.svg"},
    {"skimmer",   "mount_skimmer.svg"},
    {"jackal",    "mount_jackal.svg"},
    {"griffon",   "mount_griffon.svg"},
    {"beetle",    "mount_rollerbeetle.svg"},
    {"warclaw",   "mount_warclaw.svg"},
    {"skyscale",  "mount_skyscale.svg"},
    {"turtle",    "mount_siegeturtle.svg"},
    {"skiff",     "mount_skiff.svg"},
};

void RadialController::rebuildElements() {
  auto &elements = m_wheel->elements();
  elements.clear();

  // Build elements from enabled mounts in settings, sorted by sortOrder
  struct SortEntry {
    QString key;
    RadialElementConfig config;
  };
  QVector<SortEntry> sorted;
  for (auto it = m_settings.mounts.constBegin();
       it != m_settings.mounts.constEnd(); ++it) {
    if (it.value().enabled) {
      sorted.append({it.key(), it.value()});
    }
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const SortEntry &a, const SortEntry &b) {
              return a.config.sortOrder < b.config.sortOrder;
            });

  for (int i = 0; i < sorted.size(); ++i) {
    const auto &entry = sorted[i];
    RadialElement elem;
    elem.id = QStringLiteral("mount_") + entry.key;
    elem.displayName = entry.key;
    // Capitalize first letter for display
    if (!elem.displayName.isEmpty()) {
      elem.displayName[0] = elem.displayName[0].toUpper();
    }
    elem.enabled = true;
    elem.sortOrder = i;
    elem.colorR = 1.0f;
    elem.colorG = 1.0f;
    elem.colorB = 1.0f;
    elem.colorA = 1.0f;

    // Map settings key → SVG icon path
    QString iconFile = kMountIconMap.value(entry.key);
    if (!iconFile.isEmpty()) {
      elem.iconPath = QStringLiteral(":/radial/svg/") + iconFile;
    }

    elements.append(elem);
  }

  m_wheel->rebuildVisibleElements();
  qInfo() << "RadialController: Rebuilt" << elements.size()
          << "mount elements from settings";
}

// ============================================================================
// Icon Texture Loading (called once after renderer init)
// ============================================================================

void RadialController::loadIconTextures(D3D11Context *ctx) {
  if (m_iconsLoaded) {
    return;
  }

  auto &elements = m_wheel->elements();
  int loaded = 0;

  for (auto &elem : elements) {
    if (elem.iconPath.isEmpty()) {
      continue;
    }
    elem.iconSRV = m_renderer->loadIconTexture(ctx, elem.iconPath);
    if (elem.iconSRV) {
      ++loaded;
    }
  }

  m_iconsLoaded = true;
  qInfo() << "RadialController: Loaded" << loaded << "/" << elements.size()
          << "icon textures";
}

// ============================================================================
// Hotkey Polling
// ============================================================================

void RadialController::pollHotkey() {
  if (!m_isFocused || m_triggerVK == 0) {
    return; // Not focused or no hotkey configured
  }

  // Check modifier keys match before accepting the trigger key
  bool modifiersOk = true;
  if (m_triggerModifiers & static_cast<int>(Qt::ControlModifier))
    modifiersOk &= (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  if (m_triggerModifiers & static_cast<int>(Qt::ShiftModifier))
    modifiersOk &= (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  if (m_triggerModifiers & static_cast<int>(Qt::AltModifier))
    modifiersOk &= (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

  bool keyDown = modifiersOk && (GetAsyncKeyState(m_triggerVK) & 0x8000) != 0;

  if (keyDown && !m_wasKeyDown) {
    // Key just pressed → activate wheel at screen center
    m_wheel->activate(0.5f, 0.5f);
    // Signal the overlay window that GPU rendering is needed
    if (m_overlayWindow) {
      m_overlayWindow->setWheelNeedsRendering(true);
    }
  } else if (!keyDown && m_wasKeyDown && m_wheel->isActive()) {
    // Key released → deactivate and fire selection
    QString selectedId = m_wheel->deactivate();
    if (!selectedId.isEmpty()) {
      // Phase 2: send keybind to GW2 via SendInput
      qInfo() << "RadialController: Element selected:" << selectedId;
    }
  }

  m_wasKeyDown = keyDown;
}

// ============================================================================
// Rendering
// ============================================================================

bool RadialController::renderFrame(D3D11Context *ctx) {
  if (!ctx || !ctx->isInitialized()) {
    return false;
  }

  // Lazy-init renderer on first render
  if (!m_renderer->isInitialized()) {
    if (!m_renderer->initialize(ctx)) {
      qCritical() << "RadialController: Renderer initialization failed";
      return false;
    }
    // Load icon textures once renderer has a device
    loadIconTextures(ctx);
  }

  // Calculate delta time
  qint64 now = m_frameTimer.elapsed();
  float deltaMs = static_cast<float>(now - m_lastFrameMs);
  m_lastFrameMs = now;

  // Cap delta to prevent huge jumps
  if (deltaMs > 100.0f) {
    deltaMs = 16.0f;
  }

  // Poll hotkey state (cheap — just GetAsyncKeyState calls)
  pollHotkey();

  const bool isActive = m_wheel->isActive();

  // ---- Transition detection: active → inactive → begin fade-out ----
  if (m_wasWheelActive && !isActive && m_fadeAlpha <= 0.0f) {
    // Save the wheel's configured opacity so we can restore it after fade
    m_savedGlobalOpacity = m_wheel->globalOpacity();
    m_fadeAlpha = 1.0f;
    qInfo() << "[DIAG] RadialController: WHEEL_FADE_START";
  }
  m_wasWheelActive = isActive;

  // ---- Fade-out in progress: render at decreasing opacity ----
  if (!isActive && m_fadeAlpha > 0.0f) {
    float deltaSec = deltaMs / 1000.0f;
    // Fade speed: 1.0 / 0.15s = ~6.67 units/sec for 150ms fade
    static constexpr float kFadeOutSpeed = 6.67f;
    m_fadeAlpha -= deltaSec * kFadeOutSpeed;

    if (m_fadeAlpha <= 0.0f) {
      // Fade complete — restore original opacity, present clear frame
      m_fadeAlpha = 0.0f;
      m_wheel->setGlobalOpacity(m_savedGlobalOpacity);

      // Present one transparent frame to fully clear the swap chain
      ctx->beginFrame();
      qInfo() << "[DIAG] RadialController: WHEEL_FADE_COMPLETE";
      // Signal the overlay window that GPU rendering is no longer needed
      if (m_overlayWindow) {
        m_overlayWindow->setWheelNeedsRendering(false);
      }
      return true;  // Caller calls endFrame() → Present
    }

    // Still fading — render wheel at reduced opacity
    m_wheel->setGlobalOpacity(m_savedGlobalOpacity * m_fadeAlpha);

    ctx->beginFrame();

    // Get cursor position for consistent visual
    POINT cursor = {};
    GetCursorPos(&cursor);
    if (m_overlayWindow && m_overlayWindow->hwnd()) {
      ScreenToClient(m_overlayWindow->hwnd(), &cursor);
    }

    // Draw the wheel at faded opacity
    m_renderer->drawWheel(ctx, m_wheel);

    // Draw each element
    auto &visible = m_wheel->visibleElements();
    for (int i = 0; i < visible.size(); ++i) {
      m_renderer->drawElement(ctx, m_wheel, visible[i], i, visible.size());
    }

    // Draw cursor glow (also faded)
    m_renderer->drawCursor(ctx, cursor.x, cursor.y, ctx->width(), ctx->height(),
                            m_wheel->animationTimer(), m_wheel->globalOpacity());

    return true;  // Caller calls endFrame() → Present
  }

  // ---- Wheel not active and not fading — skip all GPU work ----
  if (!isActive) {
    return false;
  }

  // ---- Wheel IS active — begin GPU frame and render ----
  ctx->beginFrame();

  // Get current mouse position (screen → client coords)
  POINT cursor = {};
  GetCursorPos(&cursor);
  if (m_overlayWindow && m_overlayWindow->hwnd()) {
    ScreenToClient(m_overlayWindow->hwnd(), &cursor);
  }

  // Tick wheel animations + hover detection
  m_wheel->tick(deltaMs, cursor.x, cursor.y, ctx->width(), ctx->height());

  // Draw the wheel
  m_renderer->drawWheel(ctx, m_wheel);

  // Draw each element
  auto &visible = m_wheel->visibleElements();
  for (int i = 0; i < visible.size(); ++i) {
    m_renderer->drawElement(ctx, m_wheel, visible[i], i, visible.size());
  }

  // Draw cursor glow
  m_renderer->drawCursor(ctx, cursor.x, cursor.y, ctx->width(), ctx->height(),
                          m_wheel->animationTimer(), m_wheel->globalOpacity());

  return true; // Content drawn — caller should call endFrame/Present
}

/**
 * @file RadialController.cpp
 * @brief Central controller for radial menu rendering
 *
 * Three independent radial wheels (mount, novelty, marker), each with
 * its own hotkey. Only one wheel can be active at a time.
 */

// clang-format off
#include <windows.h>
// clang-format on

#include "RadialController.h"

#include "RadialInputSender.h"

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

  // Create three independent wheels
  m_mountState.wheel = new RadialWheel();
  m_mountState.wheelType = QStringLiteral("mount");

  m_noveltyState.wheel = new RadialWheel();
  m_noveltyState.wheelType = QStringLiteral("novelty");

  m_markerState.wheel = new RadialWheel();
  m_markerState.wheelType = QStringLiteral("marker");
}

RadialController::~RadialController() {
  stop();
  delete m_renderer;
  delete m_mountState.wheel;
  delete m_noveltyState.wheel;
  delete m_markerState.wheel;
}

// ============================================================================
// Lifecycle
// ============================================================================

void RadialController::start() {
  if (m_overlayWindow) {
    return;
  }

  m_overlayWindow = new RadialOverlayWindow(m_mumbleLink, this);
  m_overlayWindow->setTargetPid(m_targetPid);
  m_overlayWindow->setHideOnUnfocus(false);

  m_overlayWindow->setRenderCallback(
      [this](D3D11Context *ctx) -> bool { return renderFrame(ctx); });
  m_overlayWindow->setIdleCallback([this]() { pollHotkey(); });

  m_frameTimer.start();
  m_lastFrameMs = m_frameTimer.elapsed();

  m_overlayWindow->startTracking();
  qInfo() << "RadialController: Started for PID:" << m_targetPid;
}

void RadialController::stop() {
  if (m_overlayWindow) {
    m_overlayWindow->stopTracking();
    delete m_overlayWindow;
    m_overlayWindow = nullptr;
  }

  m_renderer->shutdown();
  qInfo() << "RadialController: Stopped";
}

void RadialController::startHeadless() {
  m_headless = true;
  m_frameTimer.start();
  m_lastFrameMs = m_frameTimer.elapsed();
  qInfo() << "RadialController: Started (headless) for PID:" << m_targetPid;
}

void RadialController::invalidateGPUResources() {
  m_renderer->shutdown();

  // Clear icon SRVs from ALL wheels
  auto clearIcons = [](RadialWheel *wheel) {
    for (auto &elem : wheel->elements()) {
      elem.iconSRV.Reset();
    }
  };
  clearIcons(m_mountState.wheel);
  clearIcons(m_noveltyState.wheel);
  clearIcons(m_markerState.wheel);

  m_iconsLoaded = false;
  qInfo() << "RadialController: GPU resources invalidated";
}

bool RadialController::needsRendering() const {
  bool anyActive = (m_mountState.wheel->isActive() ||
                    m_noveltyState.wheel->isActive() ||
                    m_markerState.wheel->isActive());
  return anyActive || m_fadeAlpha > 0.0f;
}

void RadialController::renderToTarget(D3D11Context *ctx, int cursorX, int cursorY,
                                       int viewW, int viewH) {
  if (!ctx || !ctx->isInitialized()) return;

  if (!m_renderer->isInitialized()) {
    if (!m_renderer->initialize(ctx)) {
      qCritical() << "RadialController: Renderer initialization failed";
      return;
    }
  }
  if (!m_iconsLoaded) {
    loadIconTextures(ctx);
  }

  qint64 now = m_frameTimer.elapsed();
  float deltaMs = static_cast<float>(now - m_lastFrameMs);
  m_lastFrameMs = now;
  if (deltaMs > 100.0f) deltaMs = 16.0f;

  // Determine which wheel to render
  RadialWheel *wheel = m_activeWheel;
  if (!wheel) return;

  const bool isActive = wheel->isActive();

  if (m_wasWheelActive && !isActive && m_fadeAlpha <= 0.0f) {
    m_savedGlobalOpacity = wheel->globalOpacity();
    m_fadeAlpha = 1.0f;
  }
  m_wasWheelActive = isActive;

  if (!isActive && m_fadeAlpha > 0.0f) {
    float deltaSec = deltaMs / 1000.0f;
    static constexpr float kFadeOutSpeed = 6.67f;
    m_fadeAlpha -= deltaSec * kFadeOutSpeed;

    if (m_fadeAlpha <= 0.0f) {
      m_fadeAlpha = 0.0f;
      wheel->setGlobalOpacity(m_savedGlobalOpacity);
      m_activeWheel = nullptr;
      m_activeWheelState = nullptr;
      return;
    }

    wheel->setGlobalOpacity(m_savedGlobalOpacity * m_fadeAlpha);
  }

  if (!isActive && m_fadeAlpha <= 0.0f) {
    m_activeWheel = nullptr;
    m_activeWheelState = nullptr;
    return;
  }

  wheel->tick(deltaMs, cursorX, cursorY, viewW, viewH);

  m_renderer->drawWheel(ctx, wheel);
  auto &visible = wheel->visibleElements();
  for (int i = 0; i < visible.size(); ++i) {
    m_renderer->drawElement(ctx, wheel, visible[i], i, visible.size());
  }
  m_renderer->drawCursor(ctx, cursorX, cursorY, viewW, viewH,
                          wheel->animationTimer(), wheel->globalOpacity());
}

void RadialController::onSettingsReceived(const QJsonObject &settings) {
  Q_UNUSED(settings);
  qWarning() << "RadialController::onSettingsReceived called directly —"
             << "use applySettings() instead";
}

void RadialController::applySettings(const RadialSettings &settings) {
  m_settings = settings;

  // Migration: lockCameraWhenOverlayed is not yet fully supported
  // (virtual cursor system needs more work). Force off to prevent
  // cursor from being trapped in a 1px ClipCursor rect.
  m_settings.lockCameraWhenOverlayed = false;

  // Migration: resetCursorAfterKeybind moves the mouse pointer via
  // SetCursorPos — user rule forbids any cursor movement by the radial.
  m_settings.resetCursorAfterKeybind = false;

  // Configure mount wheel hotkey
  if (m_settings.mountWheelEnabled) {
    m_mountState.triggerVK = m_settings.mountHotkey;
    m_mountState.triggerModifiers = m_settings.mountHotkeyModifiers;
  } else {
    m_mountState.triggerVK = 0;
    m_mountState.triggerModifiers = 0;
  }

  // Configure novelty wheel hotkey
  if (m_settings.noveltyWheelEnabled) {
    m_noveltyState.triggerVK = m_settings.noveltyHotkey;
    m_noveltyState.triggerModifiers = m_settings.noveltyHotkeyModifiers;
  } else {
    m_noveltyState.triggerVK = 0;
    m_noveltyState.triggerModifiers = 0;
  }

  // Configure marker wheel hotkey
  if (m_settings.markerWheelEnabled) {
    m_markerState.triggerVK = m_settings.markerHotkey;
    m_markerState.triggerModifiers = m_settings.markerHotkeyModifiers;
  } else {
    m_markerState.triggerVK = 0;
    m_markerState.triggerModifiers = 0;
  }

  qInfo() << "RadialController: Settings applied"
          << "mount VK:" << m_mountState.triggerVK
          << "novelty VK:" << m_noveltyState.triggerVK
          << "marker VK:" << m_markerState.triggerVK
          << "wheelScale:" << m_settings.wheelScale
          << "opacity:" << m_settings.opacity;

  // Dump all mount keybinds for diagnostics
  for (auto it = m_settings.mounts.begin(); it != m_settings.mounts.end(); ++it) {
    qInfo() << "[DEV] Mount keybind:" << it.key()
            << "VK:" << it->scanCode << "mod:" << it->modifiers
            << "enabled:" << it->enabled;
  }

  // Apply display settings to all wheels
  auto configureWheel = [&](RadialWheel *wheel) {
    wheel->setCenterScale(m_settings.centerScale);
    wheel->setGlobalOpacity(m_settings.opacity);
    if (m_settings.animationTimeMs > 0) {
      float speedMultiplier = 150.0f / static_cast<float>(m_settings.animationTimeMs);
      wheel->setAnimationSpeed(speedMultiplier);
    }
  };
  configureWheel(m_mountState.wheel);
  configureWheel(m_noveltyState.wheel);
  configureWheel(m_markerState.wheel);

  m_renderer->setWheelScale(0.72f * m_settings.wheelScale);
  rebuildElements();
  m_iconsLoaded = false;
}

void RadialController::onFocusChanged(bool focused) {
  m_isFocused = focused;

  qInfo() << "[DIAG] RadialController: FOCUS_CHANGED"
          << "focused:" << focused;

  if (!focused) {
    deactivateActiveWheel();

    if (m_fadeAlpha > 0.0f && m_activeWheel) {
      m_activeWheel->setGlobalOpacity(m_savedGlobalOpacity);
      m_fadeAlpha = 0.0f;
      m_wasWheelActive = false;
      m_activeWheel = nullptr;
      m_activeWheelState = nullptr;
    }

    if (!m_headless && m_overlayWindow) {
      auto *ctx = m_overlayWindow->d3dContext();
      if (ctx && ctx->isInitialized()) {
        ctx->beginFrame();
        ctx->endFrame();
      }
      m_overlayWindow->setWheelNeedsRendering(false);
      m_overlayWindow->setRenderingEnabled(false);
    }
  } else {
    if (!m_headless && m_overlayWindow) {
      m_overlayWindow->setRenderingEnabled(true);
    }
  }
}

void RadialController::setLoadingScreen(bool loading) {
  if (m_loadingScreen == loading) return;
  m_loadingScreen = loading;

  if (loading) {
    deactivateActiveWheel();

    if (m_fadeAlpha > 0.0f && m_activeWheel) {
      m_activeWheel->setGlobalOpacity(m_savedGlobalOpacity);
      m_fadeAlpha = 0.0f;
      m_wasWheelActive = false;
      m_activeWheel = nullptr;
      m_activeWheelState = nullptr;
    }
  }
}

void RadialController::deactivateActiveWheel() {
  if (m_activeWheel && m_activeWheel->isActive()) {
    m_activeWheel->deactivate();
    if (m_activeWheelState) {
      m_activeWheelState->wasKeyDown = false;
      m_activeWheelState->noHoldOpen = false;
    }
    if (m_settings.resetCursorAfterKeybind) {
      RadialInputSender::restoreCursorPosition();
    }
    qInfo() << "[DIAG] RadialController: WHEEL_DEACTIVATED";
  }
}

// ============================================================================
// Element Rebuilding (from RadialSettings)
// ============================================================================

// Static mapping: settings key -> SVG icon filename
static const QMap<QString, QString> kMountIconMapSvg = {
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

// Static mapping: settings key -> Classic GW2 PNG icon filename
static const QMap<QString, QString> kMountIconMapPng = {
    {"raptor",    "mount_raptor.png"},
    {"springer",  "mount_springer.png"},
    {"skimmer",   "mount_skimmer.png"},
    {"jackal",    "mount_jackal.png"},
    {"griffon",   "mount_griffon.png"},
    {"beetle",    "mount_rollerbeetle.png"},
    {"warclaw",   "mount_warclaw.png"},
    {"skyscale",  "mount_skyscale.png"},
    {"turtle",    "mount_siegeturtle.png"},
    // Note: skiff intentionally omitted — uses text label fallback
    {"fishing",   "mount_fishing.png"},
};

// GW2Radial-inspired per-mount tint colors (applied via adjustedColor in shader)
// Source: GW2Radial MountWheel::GetMountColorFromType()
struct MountColor { float r, g, b; };
static const QMap<QString, MountColor> kMountColors = {
    {"raptor",    {213/255.f, 100/255.f,  89/255.f}},
    {"springer",  {212/255.f, 198/255.f,  94/255.f}},
    {"skimmer",   {108/255.f, 128/255.f, 213/255.f}},
    {"jackal",    {120/255.f, 183/255.f, 197/255.f}},
    {"griffon",   {136/255.f, 123/255.f, 195/255.f}},
    {"beetle",    {199/255.f, 131/255.f,  68/255.f}},
    {"warclaw",   {181/255.f, 255/255.f, 244/255.f}},
    {"skyscale",  {211/255.f, 142/255.f, 244/255.f}},
    {"turtle",    { 56/255.f, 228/255.f,  85/255.f}},
    {"skiff",     { 64/255.f, 196/255.f, 225/255.f}},  // Ocean blue
    {"fishing",   {100/255.f, 149/255.f, 237/255.f}},  // Cornflower blue
};

// Novelty tint colors
static const QMap<QString, MountColor> kNoveltyColors = {
    {"chair",         {218/255.f, 165/255.f,  32/255.f}},  // Goldenrod
    {"instrument",    {180/255.f, 130/255.f, 200/255.f}},  // Light purple
    {"heldItem",      {200/255.f, 200/255.f, 200/255.f}},  // Silver
    {"travelToy",     {100/255.f, 200/255.f, 100/255.f}},  // Light green
    {"tonic",         {130/255.f, 200/255.f, 220/255.f}},  // Light cyan
    {"jadeWaypoint",  { 80/255.f, 220/255.f, 180/255.f}},  // Jade green
    {"fishing",       {100/255.f, 149/255.f, 237/255.f}},  // Cornflower blue
    {"scanForRift",   {200/255.f, 100/255.f, 200/255.f}},  // Magenta
    {"summonDoorway", {220/255.f, 180/255.f,  80/255.f}},  // Gold
};

// Marker tint colors
static const QMap<QString, MountColor> kMarkerColors = {
    {"arrow",    {255/255.f, 100/255.f, 100/255.f}},  // Red
    {"circle",   {100/255.f, 255/255.f, 100/255.f}},  // Green
    {"heart",    {255/255.f, 100/255.f, 150/255.f}},  // Pink
    {"square",   {100/255.f, 100/255.f, 255/255.f}},  // Blue
    {"star",     {255/255.f, 255/255.f, 100/255.f}},  // Yellow
    {"spiral",   {200/255.f, 150/255.f, 255/255.f}},  // Purple
    {"triangle", {100/255.f, 255/255.f, 255/255.f}},  // Cyan
    {"x",        {255/255.f, 150/255.f,  50/255.f}},  // Orange
    {"clear",    {200/255.f, 200/255.f, 200/255.f}},  // Gray
};

// Novelty PNG icon map (fishing has a PNG; others use SVG fallback for now)
static const QMap<QString, QString> kNoveltyIconMapPng = {
    {"fishing",   "mount_fishing.png"},
};

static void buildWheelElements(
    RadialWheel *wheel,
    const QMap<QString, RadialElementConfig> &configs,
    const QString &idPrefix,
    const QMap<QString, MountColor> &colors,
    const QMap<QString, QString> &pngIconMap,
    const QMap<QString, QString> &svgIconMap,
    bool usePng) {

  auto &elements = wheel->elements();
  elements.clear();

  struct SortEntry {
    QString key;
    RadialElementConfig config;
  };
  QVector<SortEntry> sorted;
  for (auto it = configs.constBegin(); it != configs.constEnd(); ++it) {
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
    elem.id = idPrefix + entry.key;
    elem.displayName = entry.key;
    if (!elem.displayName.isEmpty()) {
      elem.displayName[0] = elem.displayName[0].toUpper();
    }
    elem.enabled = true;
    elem.sortOrder = i;

    // Apply tint color
    auto colorIt = colors.find(entry.key);
    if (colorIt != colors.end()) {
      elem.colorR = colorIt->r;
      elem.colorG = colorIt->g;
      elem.colorB = colorIt->b;
    }
    elem.colorA = 1.0f;
    elem.premultipliedAlpha = usePng;

    // Map settings key -> icon path
    if (usePng) {
      QString iconFile = pngIconMap.value(entry.key);
      if (!iconFile.isEmpty()) {
        elem.iconPath = QStringLiteral(":/radial/png/") + iconFile;
      } else {
        // Fallback to SVG if no PNG exists
        QString svgFile = svgIconMap.value(entry.key);
        if (!svgFile.isEmpty()) {
          elem.iconPath = QStringLiteral(":/radial/svg/") + svgFile;
          elem.premultipliedAlpha = false;
        }
      }
    } else {
      QString iconFile = svgIconMap.value(entry.key);
      if (!iconFile.isEmpty()) {
        elem.iconPath = QStringLiteral(":/radial/svg/") + iconFile;
      }
    }

    elements.append(elem);
  }

  wheel->rebuildVisibleElements();
}

void RadialController::rebuildElements() {
  bool usePng = (m_settings.iconStyle == QStringLiteral("png_mit"));

  // Empty icon maps for novelties/markers that don't have icons yet
  static const QMap<QString, QString> kEmptyMap;

  // Build mount wheel
  buildWheelElements(m_mountState.wheel, m_settings.mounts,
                     QStringLiteral("mount_"), kMountColors,
                     kMountIconMapPng, kMountIconMapSvg, usePng);
  qInfo() << "RadialController: Rebuilt" << m_mountState.wheel->elements().size()
          << "mount elements";

  // Build novelty wheel
  buildWheelElements(m_noveltyState.wheel, m_settings.novelties,
                     QStringLiteral("novelty_"), kNoveltyColors,
                     kNoveltyIconMapPng, kEmptyMap, usePng);
  qInfo() << "RadialController: Rebuilt" << m_noveltyState.wheel->elements().size()
          << "novelty elements";

  // Build marker wheel
  buildWheelElements(m_markerState.wheel, m_settings.markers,
                     QStringLiteral("marker_"), kMarkerColors,
                     kEmptyMap, kEmptyMap, usePng);
  qInfo() << "RadialController: Rebuilt" << m_markerState.wheel->elements().size()
          << "marker elements";
}

// ============================================================================
// Icon Texture Loading (called once after renderer init)
// ============================================================================

void RadialController::loadIconTextures(D3D11Context *ctx) {
  if (m_iconsLoaded) {
    return;
  }

  int loaded = 0;
  auto loadForWheel = [&](RadialWheel *wheel) {
    for (auto &elem : wheel->elements()) {
      if (!elem.iconPath.isEmpty()) {
        elem.iconSRV = m_renderer->loadIconTexture(ctx, elem.iconPath);
        if (elem.iconSRV) ++loaded;
      } else if (!elem.displayName.isEmpty()) {
        // No icon file — create a text label texture (e.g., "S" for Skiff)
        QString letter = elem.displayName.left(1).toUpper();
        elem.iconSRV = m_renderer->createLabelTexture(ctx, letter);
        elem.premultipliedAlpha = false; // QPainter renders straight alpha
        if (elem.iconSRV) ++loaded;
      }
    }
  };
  loadForWheel(m_mountState.wheel);
  loadForWheel(m_noveltyState.wheel);
  loadForWheel(m_markerState.wheel);

  m_iconsLoaded = true;
  qInfo() << "RadialController: Loaded" << loaded << "icon textures across all wheels";
}

// ============================================================================
// Hotkey Polling
// ============================================================================

const RadialElementConfig* RadialController::findElementConfig(
    const QString &settingsKey, const WheelState &ws) const {
  if (ws.wheelType == QStringLiteral("mount")) {
    auto it = m_settings.mounts.find(settingsKey);
    return (it != m_settings.mounts.end()) ? &(*it) : nullptr;
  } else if (ws.wheelType == QStringLiteral("novelty")) {
    auto it = m_settings.novelties.find(settingsKey);
    return (it != m_settings.novelties.end()) ? &(*it) : nullptr;
  } else if (ws.wheelType == QStringLiteral("marker")) {
    auto it = m_settings.markers.find(settingsKey);
    return (it != m_settings.markers.end()) ? &(*it) : nullptr;
  }
  return nullptr;
}

// MumbleLink mountIndex → settings key mapping
static const QMap<uint8_t, QString> kMountIndexToKey = {
    {1,  "raptor"},
    {2,  "springer"},
    {3,  "skimmer"},
    {4,  "jackal"},
    {5,  "griffon"},
    {6,  "beetle"},
    {7,  "warclaw"},
    {8,  "skyscale"},
    {9,  "turtle"},
    {10, "skiff"},
};

// Reverse mapping: settings key → mountIndex (for same-mount detection)
static const QMap<QString, uint8_t> kMountKeyToIndex = {
    {"raptor",   1},
    {"springer", 2},
    {"skimmer",  3},
    {"jackal",   4},
    {"griffon",  5},
    {"beetle",   6},
    {"warclaw",  7},
    {"skyscale", 8},
    {"turtle",   9},
    {"skiff",   10},
};

void RadialController::handleSelection(const QString &selectedId,
                                        const WheelState &ws) {
  if (selectedId.isEmpty()) {
    // Center selected — apply center behavior
    switch (m_settings.centerBehavior) {
    case RadialCenterBehavior::MountDismount:
      if (m_settings.dismountScanCode != 0) {
        qInfo() << "RadialController: Center behavior → Mount/Dismount"
                << "VK:" << m_settings.dismountScanCode
                << "mod:" << m_settings.dismountModifiers;
        RadialInputSender::sendKeybind(m_settings.dismountScanCode,
                                       m_settings.dismountModifiers,
                                       ws.triggerVK);
      } else {
        qWarning() << "RadialController: Center MountDismount but no"
                    << "dismount keybind configured";
      }
      break;
    case RadialCenterBehavior::Nothing:
    case RadialCenterBehavior::Previous:
    case RadialCenterBehavior::Favorite:
    case RadialCenterBehavior::PassToGame:
    default:
      break;
    }
    return;
  }

  // Extract settings key from element ID (e.g., "mount_raptor" -> "raptor")
  int underscorePos = selectedId.indexOf('_');
  QString settingsKey =
      (underscorePos >= 0) ? selectedId.mid(underscorePos + 1) : selectedId;

  const RadialElementConfig *cfg = findElementConfig(settingsKey, ws);
  if (!cfg || cfg->scanCode == 0) {
    qWarning() << "RadialController: No keybind configured for"
               << selectedId
               << "(set matching GW2 keybinds in Radial Settings)";
    return;
  }

  // --- Fast Mount Swap ---
  // If fast swap is ON and we auto-dismounted when the radial opened,
  // wait for the remaining dismount cooldown (400ms), then send the mount key.
  if (m_settings.fastMountSwap &&
      ws.wheelType == QStringLiteral("mount") &&
      m_fastSwapDismountSent) {
    m_fastSwapDismountSent = false; // Reset flag

    qint64 elapsed = m_fastSwapDismountTimer.elapsed();
    qint64 remaining = kDismountCooldownMs - elapsed;

    qInfo() << "RadialController: [Fast Swap] Mount selected:" << settingsKey
            << "VK:" << cfg->scanCode << "mod:" << cfg->modifiers
            << "elapsed:" << elapsed << "ms remaining:" << remaining << "ms";

    if (remaining > 0) {
      qInfo() << "[Fast Swap] Waiting" << remaining << "ms dismount cooldown...";
      Sleep(static_cast<DWORD>(remaining));
    }

    // Send mount keybind (triggerVK=0: trigger already released)
    RadialInputSender::sendKeybind(cfg->scanCode, cfg->modifiers, 0);

    // Record mount time for 600ms mount cooldown on next swap
    m_lastMountSentTimer.start();
    m_lastMountSentValid = true;

    qInfo() << "[Fast Swap] Mount keybind sent:" << settingsKey;
    return;
  }

  // Normal path: send the selected element's keybind directly
  qInfo() << "RadialController: Sending keybind for" << selectedId
          << "VK:" << cfg->scanCode << "modifiers:" << cfg->modifiers;
  RadialInputSender::sendKeybind(cfg->scanCode, cfg->modifiers,
                                 ws.triggerVK);
}

bool RadialController::pollWheelHotkey(WheelState &ws) {
  if (ws.triggerVK == 0) {
    ws.wasKeyDown = false;
    return false;
  }

  // Check modifier keys (GW2 bitmask: 1=Shift, 2=Ctrl, 4=Alt)
  bool modifiersOk = true;
  if (ws.triggerModifiers & 1)
    modifiersOk &= (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  if (ws.triggerModifiers & 2)
    modifiersOk &= (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  if (ws.triggerModifiers & 4)
    modifiersOk &= (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

  bool keyDown = modifiersOk && (GetAsyncKeyState(ws.triggerVK) & 0x8000) != 0;
  bool consumed = false;

  if (m_settings.noHoldMode) {
    // === No-hold mode: tap to open, tap again to select ===
    if (keyDown && !ws.wasKeyDown) {
      if (ws.noHoldOpen && ws.wheel->isActive()) {
        // Second press -> deactivate and fire selection
        QString selectedId = ws.wheel->deactivate();
        ws.noHoldOpen = false;

        handleSelection(selectedId, ws);

        if (m_settings.resetCursorAfterKeybind) {
          RadialInputSender::restoreCursorPosition();
        }
        consumed = true;
      } else if (!m_activeWheel || !m_activeWheel->isActive()) {
        // First press -> activate (only if no other wheel is active)
        if (m_settings.resetCursorAfterKeybind) {
          RadialInputSender::saveCursorPosition(
              m_settings.lockCameraWhenOverlayed);
        }
        ws.wheel->activate(0.5f, 0.5f);
        ws.noHoldOpen = true;
        m_activeWheel = ws.wheel;
        m_activeWheelState = &ws;
        if (m_overlayWindow) {
          m_overlayWindow->setWheelNeedsRendering(true);
        }

        // Fast Swap: auto-dismount on mount wheel open (no-hold mode)
        m_fastSwapDismountSent = false;
        if (m_settings.fastMountSwap &&
            ws.wheelType == QStringLiteral("mount") &&
            m_mumbleLink && m_mumbleLink->mountIndex() > 0) {
          if (m_settings.dismountScanCode != 0) {
            // Wait for mount cooldown (600ms after last mount) if needed
            if (m_lastMountSentValid) {
              qint64 sinceMnt = m_lastMountSentTimer.elapsed();
              qint64 mntRemain = kMountCooldownMs - sinceMnt;
              if (mntRemain > 0) {
                qInfo() << "[Fast Swap] Waiting" << mntRemain
                        << "ms mount cooldown before dismount (no-hold)";
                Sleep(static_cast<DWORD>(mntRemain));
              }
            }
            qInfo() << "[Fast Swap] Auto-dismounting on radial open (no-hold)"
                    << "mountIndex:" << m_mumbleLink->mountIndex()
                    << "VK:" << m_settings.dismountScanCode;
            RadialInputSender::sendKeybindInstant(m_settings.dismountScanCode,
                                                    m_settings.dismountModifiers);
            m_fastSwapDismountSent = true;
            m_fastSwapDismountTimer.start();
          } else {
            qWarning() << "[Fast Swap] Cannot auto-dismount:"
                       << "no Mount/Dismount keybind configured";
          }
        }

        consumed = true;
      }
    }
  } else {
    // === Hold mode (default): press-and-hold to open, release to select ===
    if (keyDown && !ws.wasKeyDown) {
      // Key just pressed
      if (m_activeWheel && m_activeWheel->isActive() && m_activeWheel != ws.wheel) {
        // Another wheel is active — deactivate it first
        deactivateActiveWheel();
      }
      if (m_settings.resetCursorAfterKeybind) {
        RadialInputSender::saveCursorPosition(
            m_settings.lockCameraWhenOverlayed);
      }
      ws.wheel->activate(0.5f, 0.5f);
      m_activeWheel = ws.wheel;
      m_activeWheelState = &ws;
      if (m_overlayWindow) {
        m_overlayWindow->setWheelNeedsRendering(true);
      }

      // Fast Swap: auto-dismount on mount wheel open
      m_fastSwapDismountSent = false;
      if (m_settings.fastMountSwap &&
          ws.wheelType == QStringLiteral("mount") &&
          m_mumbleLink && m_mumbleLink->mountIndex() > 0) {
        if (m_settings.dismountScanCode != 0) {
          // Wait for mount cooldown (600ms after last mount) if needed
          if (m_lastMountSentValid) {
            qint64 sinceMnt = m_lastMountSentTimer.elapsed();
            qint64 mntRemain = kMountCooldownMs - sinceMnt;
            if (mntRemain > 0) {
              qInfo() << "[Fast Swap] Waiting" << mntRemain
                      << "ms mount cooldown before dismount";
              Sleep(static_cast<DWORD>(mntRemain));
            }
          }
          qInfo() << "[Fast Swap] Auto-dismounting on radial open"
                  << "mountIndex:" << m_mumbleLink->mountIndex()
                  << "VK:" << m_settings.dismountScanCode;
          RadialInputSender::sendKeybindInstant(m_settings.dismountScanCode,
                                                  m_settings.dismountModifiers);
          m_fastSwapDismountSent = true;
          m_fastSwapDismountTimer.start();
        } else {
          qWarning() << "[Fast Swap] Cannot auto-dismount:"
                     << "no Mount/Dismount keybind configured";
        }
      }

      consumed = true;
    } else if (!keyDown && ws.wasKeyDown && ws.wheel->isActive()) {
      // Key released -> deactivate and fire selection
      QString selectedId = ws.wheel->deactivate();

      handleSelection(selectedId, ws);

      if (m_settings.resetCursorAfterKeybind) {
        RadialInputSender::restoreCursorPosition();
      }
      consumed = true;
    }
  }

  ws.wasKeyDown = keyDown;
  return consumed;
}

void RadialController::pollHotkey() {
  if (!m_isFocused || m_loadingScreen) {
    return;
  }

  // Poll all three wheels — only one can be active at a time.
  // Skip wheels that share a trigger key with the currently active wheel
  // to prevent infinite activate/deactivate cycling.
  auto shouldPoll = [this](const WheelState &ws) -> bool {
    if (m_activeWheel && m_activeWheel->isActive() &&
        m_activeWheelState && m_activeWheelState != &ws &&
        m_activeWheelState->triggerVK == ws.triggerVK &&
        m_activeWheelState->triggerModifiers == ws.triggerModifiers) {
      return false;  // Same hotkey as active wheel — skip
    }
    return true;
  };

  if (shouldPoll(m_mountState) && pollWheelHotkey(m_mountState)) return;
  if (shouldPoll(m_noveltyState) && pollWheelHotkey(m_noveltyState)) return;
  if (shouldPoll(m_markerState)) pollWheelHotkey(m_markerState);
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
  }
  if (!m_iconsLoaded) {
    loadIconTextures(ctx);
  }

  // Calculate delta time
  qint64 now = m_frameTimer.elapsed();
  float deltaMs = static_cast<float>(now - m_lastFrameMs);
  m_lastFrameMs = now;
  if (deltaMs > 100.0f) {
    deltaMs = 16.0f;
  }

  // Poll hotkey state
  pollHotkey();

  // Determine which wheel to render
  RadialWheel *wheel = m_activeWheel;
  if (!wheel) {
    // No active wheel and no fading — nothing to render
    if (m_fadeAlpha <= 0.0f) return false;
    // Should not happen, but guard
    m_fadeAlpha = 0.0f;
    return false;
  }

  const bool isActive = wheel->isActive();

  // ---- Transition detection: active -> inactive -> begin fade-out ----
  if (m_wasWheelActive && !isActive && m_fadeAlpha <= 0.0f) {
    m_savedGlobalOpacity = wheel->globalOpacity();
    m_fadeAlpha = 1.0f;
    qInfo() << "[DIAG] RadialController: WHEEL_FADE_START";
  }
  m_wasWheelActive = isActive;

  // ---- Fade-out in progress ----
  if (!isActive && m_fadeAlpha > 0.0f) {
    float deltaSec = deltaMs / 1000.0f;
    static constexpr float kFadeOutSpeed = 6.67f;
    m_fadeAlpha -= deltaSec * kFadeOutSpeed;

    if (m_fadeAlpha <= 0.0f) {
      m_fadeAlpha = 0.0f;
      wheel->setGlobalOpacity(m_savedGlobalOpacity);

      ctx->beginFrame();
      qInfo() << "[DIAG] RadialController: WHEEL_FADE_COMPLETE";
      if (m_overlayWindow) {
        m_overlayWindow->setWheelNeedsRendering(false);
      }
      m_activeWheel = nullptr;
      m_activeWheelState = nullptr;
      return true;
    }

    wheel->setGlobalOpacity(m_savedGlobalOpacity * m_fadeAlpha);

    ctx->beginFrame();

    POINT cursor = {};
    if (RadialInputSender::isCameraLocked()) {
      RadialInputSender::updateVirtualCursor(ctx->width(), ctx->height());
      cursor = RadialInputSender::virtualCursorPos();
      if (m_overlayWindow && m_overlayWindow->hwnd()) {
        ScreenToClient(m_overlayWindow->hwnd(), &cursor);
      }
    } else {
      GetCursorPos(&cursor);
      if (m_overlayWindow && m_overlayWindow->hwnd()) {
        ScreenToClient(m_overlayWindow->hwnd(), &cursor);
      }
    }

    m_renderer->drawWheel(ctx, wheel);
    auto &visible = wheel->visibleElements();
    for (int i = 0; i < visible.size(); ++i) {
      m_renderer->drawElement(ctx, wheel, visible[i], i, visible.size());
    }
    m_renderer->drawCursor(ctx, cursor.x, cursor.y, ctx->width(), ctx->height(),
                            wheel->animationTimer(), wheel->globalOpacity());

    return true;
  }

  // ---- Wheel not active and not fading ----
  if (!isActive) {
    m_activeWheel = nullptr;
    m_activeWheelState = nullptr;
    return false;
  }

  // ---- Wheel IS active ----
  ctx->beginFrame();

  POINT cursor = {};
  if (RadialInputSender::isCameraLocked()) {
    RadialInputSender::updateVirtualCursor(ctx->width(), ctx->height());
    cursor = RadialInputSender::virtualCursorPos();
    if (m_overlayWindow && m_overlayWindow->hwnd()) {
      ScreenToClient(m_overlayWindow->hwnd(), &cursor);
    }
  } else {
    GetCursorPos(&cursor);
    if (m_overlayWindow && m_overlayWindow->hwnd()) {
      ScreenToClient(m_overlayWindow->hwnd(), &cursor);
    }
  }

  wheel->tick(deltaMs, cursor.x, cursor.y, ctx->width(), ctx->height());

  auto &visible = wheel->visibleElements();

  m_renderer->drawWheel(ctx, wheel);
  for (int i = 0; i < visible.size(); ++i) {
    m_renderer->drawElement(ctx, wheel, visible[i], i, visible.size());
  }
  m_renderer->drawCursor(ctx, cursor.x, cursor.y, ctx->width(), ctx->height(),
                          wheel->animationTimer(), wheel->globalOpacity());

  return true;
}

/**
 * @file RadialController.cpp
 * @brief Main controller for the radial menu system
 *
 * Coordinates all radial components
 *
 * DO NOT ADD:
 * - Hotkey registration (belongs in HotkeyManager)
 * - Menu rendering (belongs in RadialWidget)
 * - Config persistence (belongs in RadialConfig)
 */

#include "RadialController.h"

#include <QDebug>

RadialController::RadialController(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumble(mumble), m_config(new RadialConfig(this)),
      m_engine(new RadialEngine(mumble, this)),
      m_widget(new RadialWidget(m_engine)),
      m_hotkeyManager(new HotkeyManager(this)) {
  m_engine->setConfig(m_config);

  // Connect signals
  connect(m_hotkeyManager, &HotkeyManager::hotkeyPressed, this,
          &RadialController::onHotkeyPressed);
  connect(m_config, &RadialConfig::menusChanged, this,
          &RadialController::onMenusChanged);

  // CRITICAL: Only register hotkeys when GW2 is actually running
  // RegisterHotKey() swallows keypresses SYSTEM-WIDE, so having them
  // active when the game isn't running steals keyboard input from
  // every other application (including the user's text editors).
  if (m_mumble) {
    connect(m_mumble, &MumbleLink::connectionChanged, this,
            &RadialController::onGameConnectionChanged);
  }
}

RadialController::~RadialController() {
  stop();
  delete m_widget;
}

void RadialController::start() {
  // DON'T register hotkeys here - they'll be registered when GW2 connects
  // via onGameConnectionChanged(). Registering on startup steals keyboard
  // input from the entire system.
  if (m_mumble && m_mumble->isConnected()) {
    registerMenuHotkeys();
  }
  qInfo()
      << "Radial menu system started (hotkeys will activate when GW2 connects)";
}

void RadialController::stop() {
  m_hotkeyManager->unregisterAll();
  m_widget->hide();
}

void RadialController::setMumbleLink(MumbleLink *mumble) {
  if (m_mumble == mumble) return;

  if (m_mumble) {
    disconnect(m_mumble, &MumbleLink::connectionChanged, this,
               &RadialController::onGameConnectionChanged);
  }

  m_mumble = mumble;
  m_engine->setMumbleLink(mumble);

  if (m_mumble) {
    connect(m_mumble, &MumbleLink::connectionChanged, this,
            &RadialController::onGameConnectionChanged);
    
    // Check initial state
    onGameConnectionChanged(m_mumble->isConnected());
  } else {
    onGameConnectionChanged(false);
  }
}

void RadialController::triggerMenu(const QString &menuId) {
  m_engine->showMenu(menuId);
}

void RadialController::onHotkeyPressed(const QString &id) {
  // Only activate radial menu if GW2 is actually running
  if (!m_mumble || !m_mumble->isConnected()) {
    return; // Game not running - don't steal keyboard focus
  }

  // ID format: "radial_<menuId>"
  if (id.startsWith("radial_")) {
    QString menuId = id.mid(7);
    m_activeHotkeyId = id;
    m_engine->showMenu(menuId);
  }
}

void RadialController::onHotkeyReleased(const QString &id) {
  Q_UNUSED(id);
  m_engine->hideMenu();
  m_activeHotkeyId.clear();
}

void RadialController::onMenusChanged() {
  // Re-register hotkeys when menus change, but ONLY if GW2 is running
  if (m_mumble && m_mumble->isConnected()) {
    registerMenuHotkeys();
  }
}

void RadialController::onGameConnectionChanged(bool connected) {
  if (connected) {
    // TODO: Add proper enable/disable toggle for radial system.
    // For now, skip hotkey registration — it intercepts global keyboard input
    // and the feature is not yet user-configurable for on/off.
    qInfo() << "Radial: GW2 connected (hotkeys disabled — needs toggle)";
  } else {
    m_hotkeyManager->unregisterAll();
    m_widget->hide();
    qInfo() << "Radial: GW2 disconnected - hotkeys unregistered";
  }
}

void RadialController::registerMenuHotkeys() {
  m_hotkeyManager->unregisterAll();

  for (const auto &menu : m_config->menus()) {
    if (menu.hotkey.isEmpty())
      continue;

    QString hotkeyId = "radial_" + menu.id;
    m_hotkeyManager->registerHotkey(hotkeyId, menu.hotkey);
  }
}

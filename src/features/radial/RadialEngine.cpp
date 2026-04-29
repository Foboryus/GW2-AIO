/**
 * @file RadialEngine.cpp
 * @brief Core engine for radial menu input handling and state
 *
 * DO NOT ADD:
 * - UI rendering (belongs in RadialWidget)
 * - Configuration persistence (belongs in RadialConfig)
 */

#include "RadialEngine.h"

#include <QCursor>
#include <QDebug>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

RadialEngine::RadialEngine(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumble(mumble) {}

void RadialEngine::showMenu(const QString &menuId) {
  if (!m_config)
    return;

  RadialMenu *menu = m_config->getMenu(menuId);
  if (!menu)
    return;

  // Check condition
  if (!checkCondition(*menu)) {
    return;
  }

  m_activeMenu = menu;
  m_menuCenter = QCursor::pos();
  m_hoveredIndex = -1;

  emit menuShown(m_activeMenu);
}

void RadialEngine::hideMenu() {
  if (!m_activeMenu)
    return;

  // Execute selection if hovering
  if (m_hoveredIndex >= 0) {
    executeSelection();
  }

  m_activeMenu = nullptr;
  m_hoveredIndex = -1;

  emit menuHidden();
}

void RadialEngine::updateHover(const QPoint &cursorPos) {
  if (!m_activeMenu)
    return;

  int newIndex = calculateHoveredIndex(cursorPos);

  if (newIndex != m_hoveredIndex) {
    m_hoveredIndex = newIndex;
    emit hoverChanged(m_hoveredIndex);
  }
}

void RadialEngine::executeSelection() {
  if (!m_activeMenu || m_hoveredIndex < 0)
    return;
  if (m_hoveredIndex >= m_activeMenu->items.size())
    return;

  const RadialItem &item = m_activeMenu->items[m_hoveredIndex];

  if (!item.enabled)
    return;

  // Check if we need to queue (e.g., in combat for mounts)
  // TODO: Check combat state from Mumble
  bool shouldQueue =
      false; // m_mumble->isInCombat() && m_activeMenu->queueIfBlocked;

  if (shouldQueue) {
    m_queuedItem = item;
    m_hasQueuedAction = true;
    emit queuedAction(item);
  } else {
    sendKeybind(item.keybind);
    emit itemSelected(item);
  }
}

bool RadialEngine::checkCondition(const RadialMenu &menu) const {
  switch (menu.condition) {
  case RadialCondition::Always:
    return true;

  case RadialCondition::InWvW:
    // Map types 18, 15 are WvW
    // TODO: Check from Mumble context
    return true;

  case RadialCondition::Underwater:
    // TODO: Check from Mumble position.z or context
    return true;

  case RadialCondition::OutOfCombat:
    // TODO: Check combat state
    return true;

  case RadialCondition::Mounted:
  case RadialCondition::NotMounted:
    // TODO: Check mount state from Mumble context
    return true;
  }

  return true;
}

int RadialEngine::calculateHoveredIndex(const QPoint &cursorPos) const {
  if (!m_activeMenu || m_activeMenu->items.isEmpty()) {
    return -1;
  }

  // Calculate vector from center to cursor
  int dx = cursorPos.x() - m_menuCenter.x();
  int dy = cursorPos.y() - m_menuCenter.y();

  // Check if in deadzone
  double distance = std::sqrt(dx * dx + dy * dy);
  if (distance < m_activeMenu->centerDeadzone) {
    return -1;
  }

  // Calculate angle (0 = top, clockwise)
  double angle = std::atan2(dx, -dy); // Top = 0
  if (angle < 0)
    angle += 2 * M_PI;

  // Convert to item index
  int itemCount = m_activeMenu->items.size();
  double sliceAngle = (2 * M_PI) / itemCount;

  // Offset by half slice so items are centered
  angle += sliceAngle / 2;
  if (angle >= 2 * M_PI)
    angle -= 2 * M_PI;

  int index = static_cast<int>(angle / sliceAngle);

  // Clamp to valid range
  if (index < 0)
    index = 0;
  if (index >= itemCount)
    index = itemCount - 1;

  return index;
}

void RadialEngine::sendKeybind(const QString &keys) {
  if (keys.isEmpty())
    return;

#ifdef Q_OS_WIN
  // Parse keybind and send via Windows API
  // This is simplified - full implementation would parse modifiers

  // TODO: Implement proper keybind sending with modifiers
  // For now, just log
  qInfo() << "Sending keybind:" << keys;
#endif
}

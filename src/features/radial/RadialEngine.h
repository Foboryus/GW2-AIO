#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QWidget>
#include <cmath>


#include "RadialConfig.h"
#include "RadialMenu.h"
#include "core/MumbleLink.h"


/**
 * @brief Core engine for radial menu input handling and state
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialEngine.cpp)
 */
class RadialEngine : public QObject {
  Q_OBJECT

public:
  explicit RadialEngine(MumbleLink *mumble, QObject *parent = nullptr);

  /**
   * @brief Set the config manager
   */
  void setConfig(RadialConfig *config) { m_config = config; }

  /**
   * @brief Update the MumbleLink reference dynamically (Phase 7b-2b)
   */
  void setMumbleLink(MumbleLink *mumble) { m_mumble = mumble; }

  /**
   * @brief Check if a menu is currently visible
   */
  bool isMenuVisible() const { return m_activeMenu != nullptr; }

  /**
   * @brief Get the currently active menu
   */
  const RadialMenu *activeMenu() const { return m_activeMenu; }

  /**
   * @brief Get the currently hovered item index (-1 if none)
   */
  int hoveredIndex() const { return m_hoveredIndex; }

  /**
   * @brief Get menu center position
   */
  QPoint menuCenter() const { return m_menuCenter; }

  /**
   * @brief Show a specific menu
   */
  void showMenu(const QString &menuId);

  /**
   * @brief Hide the current menu
   */
  void hideMenu();

  /**
   * @brief Update hover state based on cursor position
   */
  void updateHover(const QPoint &cursorPos);

  /**
   * @brief Execute the currently hovered item
   */
  void executeSelection();

  /**
   * @brief Check if condition is met for showing a menu
   */
  bool checkCondition(const RadialMenu &menu) const;

signals:
  void menuShown(const RadialMenu *menu);
  void menuHidden();
  void hoverChanged(int index);
  void itemSelected(const RadialItem &item);
  void queuedAction(const RadialItem &item); // When action is queued for later

private:
  void sendKeybind(const QString &keys);
  int calculateHoveredIndex(const QPoint &cursorPos) const;

  MumbleLink *m_mumble;
  RadialConfig *m_config = nullptr;

  const RadialMenu *m_activeMenu = nullptr;
  QPoint m_menuCenter;
  int m_hoveredIndex = -1;

  // Queued action (for combat delay)
  RadialItem m_queuedItem;
  bool m_hasQueuedAction = false;
};

#pragma once

#include <QObject>

#include "RadialConfig.h"
#include "RadialEngine.h"
#include "RadialMenu.h"
#include "RadialWidget.h"
#include "core/HotkeyManager.h"
#include "core/MumbleLink.h"


/**
 * @brief Main controller for the radial menu system
 *
 * Coordinates all radial components
 *
 * TODO: When MumbleLink-dependent features are added (game window containment,
 * combat detection, mount checks), RadialController must use per-instance
 * MumbleLink from OverlayInstance instead of the global one.
 * Reference: G:\Alura\GW2\GW2Radial for implementation patterns.
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialController.cpp)
 */
class RadialController : public QObject {
  Q_OBJECT

public:
  explicit RadialController(MumbleLink *mumble, QObject *parent = nullptr);
  ~RadialController();

  /**
   * @brief Initialize and start the radial system
   */
  void start();

  /**
   * @brief Stop and cleanup
   */
  void stop();

  /**
   * @brief Update the MumbleLink reference dynamically (Phase 7b-2b)
   */
  void setMumbleLink(MumbleLink *mumble);

  /**
   * @brief Get config for UI editing
   */
  RadialConfig *config() { return m_config; }

  /**
   * @brief Manually trigger a menu (for testing)
   */
  void triggerMenu(const QString &menuId);

private slots:
  void onHotkeyPressed(const QString &id);
  void onHotkeyReleased(const QString &id);
  void onMenusChanged();
  void onGameConnectionChanged(bool connected);

private:
  void registerMenuHotkeys();

  MumbleLink *m_mumble;
  RadialConfig *m_config;
  RadialEngine *m_engine;
  RadialWidget *m_widget;
  HotkeyManager *m_hotkeyManager;

  QString m_activeHotkeyId;
};

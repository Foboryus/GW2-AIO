/**
 * @file SystemTray.cpp
 * @brief System tray icon with context menu
 *
 * DO NOT ADD:
 * - Window management (belongs in MainWindow)
 * - Settings logic (belongs in SettingsManager)
 */

#include "SystemTray.h"

SystemTray::SystemTray(QObject *parent)
    : QObject(parent), m_trayIcon(new QSystemTrayIcon(this)),
      m_menu(new QMenu(nullptr)) // Note: Deleted manually in destructor
{
  // Set default icon (use app icon)
  m_trayIcon->setIcon(QApplication::windowIcon());
  m_trayIcon->setToolTip("GW2 AIO Manager");

  createContextMenu();

  connect(m_trayIcon, &QSystemTrayIcon::activated, this,
          &SystemTray::onActivated);
}

SystemTray::~SystemTray() { delete m_menu; }

void SystemTray::show() { m_trayIcon->show(); }

void SystemTray::hide() { m_trayIcon->hide(); }

void SystemTray::showNotification(const QString &title, const QString &message,
                                  QSystemTrayIcon::MessageIcon icon) {
  m_trayIcon->showMessage(title, message, icon, 5000);
}

void SystemTray::setToolTip(const QString &tip) { m_trayIcon->setToolTip(tip); }

void SystemTray::setGameRunning(bool running) {
  m_gameRunning = running;

  // Could change icon here based on game state
  QString tooltip = running ? "GW2 AIO Manager - Game Running"
                            : "GW2 AIO Manager - Game Not Running";
  setToolTip(tooltip);
}

void SystemTray::onActivated(QSystemTrayIcon::ActivationReason reason) {
  switch (reason) {
  case QSystemTrayIcon::DoubleClick:
    emit showWindowRequested();
    break;
  case QSystemTrayIcon::MiddleClick:
    emit toggleOverlayRequested();
    break;
  default:
    break;
  }
}

void SystemTray::createContextMenu() {
  m_showAction = m_menu->addAction("Show Window");
  connect(m_showAction, &QAction::triggered, this,
          &SystemTray::showWindowRequested);

  m_hideAction = m_menu->addAction("Hide Window");
  connect(m_hideAction, &QAction::triggered, this,
          &SystemTray::hideWindowRequested);

  m_menu->addSeparator();

  m_overlayAction = m_menu->addAction("Toggle Overlay");
  connect(m_overlayAction, &QAction::triggered, this,
          &SystemTray::toggleOverlayRequested);

  m_settingsAction = m_menu->addAction("Settings...");
  connect(m_settingsAction, &QAction::triggered, this,
          &SystemTray::settingsRequested);

  m_menu->addSeparator();

  m_quitAction = m_menu->addAction("Quit");
  connect(m_quitAction, &QAction::triggered, this, &SystemTray::quitRequested);

  m_trayIcon->setContextMenu(m_menu);
}

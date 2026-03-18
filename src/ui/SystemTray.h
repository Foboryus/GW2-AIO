#pragma once

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>


/**
 * @brief System tray icon with context menu
 *
 * DO NOT ADD:
 * - Inline implementations (use SystemTray.cpp)
 */
class SystemTray : public QObject {
  Q_OBJECT

public:
  explicit SystemTray(QObject *parent = nullptr);
  ~SystemTray();

  /**
   * @brief Initialize and show tray icon
   */
  void show();

  /**
   * @brief Hide tray icon
   */
  void hide();

  /**
   * @brief Show a tray notification
   */
  void showNotification(
      const QString &title, const QString &message,
      QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);

  /**
   * @brief Update tooltip
   */
  void setToolTip(const QString &tip);

  /**
   * @brief Set game running status (changes icon)
   */
  void setGameRunning(bool running);

signals:
  void showWindowRequested();
  void hideWindowRequested();
  void quitRequested();
  void settingsRequested();
  void toggleOverlayRequested();

private slots:
  void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
  void createContextMenu();

  QSystemTrayIcon *m_trayIcon;
  QMenu *m_menu;

  QAction *m_showAction;
  QAction *m_hideAction;
  QAction *m_overlayAction;
  QAction *m_settingsAction;
  QAction *m_quitAction;

  bool m_gameRunning = false;
};

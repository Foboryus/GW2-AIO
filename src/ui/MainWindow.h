#pragma once

#include <QCloseEvent>
#include <QEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#include <QWindow>

#include "core/GW2APIClient.h"
#include "core/LaunchManager.h"
#include "core/UpdateChecker.h"

class AddonManager;       // Forward declaration
class BlishModuleManager; // Forward declaration
class HotkeyManager;      // Forward declaration
class MarkerController;   // Forward declaration
class UpdateManager;      // Forward declaration

class DataService;       // Forward declaration
class LauncherWidget;    // Forward declaration
class MarkerPackBrowser; // Forward declaration
class SettingsWidget;    // Forward declaration
class NetworkWidget;     // Forward declaration

/**
 * @brief Main application window with GW2-style dark theme
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(DataService *dataService,
                      MarkerController *markerController,
                      QWidget *parent = nullptr);
  ~MainWindow() = default;

  void setGW2Path(const QString &path);

  LaunchManager *launchManager() const { return m_launchManager; }

  /**
   * @brief Check if GW2 update is available for specific path (using cached
   * data)
   * @param gw2Path The installation path to check (uses global if empty)
   * @return true if OK to proceed, false if user chose to update instead
   */
  Q_INVOKABLE bool
  checkGW2BuildBeforeLaunch(const QString &gw2Path = QString());

protected:
  void closeEvent(QCloseEvent *event) override;
  bool event(QEvent *event) override;
  bool nativeEvent(const QByteArray &eventType, void *message,
                   qintptr *result) override;

  // For frameless window dragging
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  void setupUI();
  void setupNavigation();
  void setupTrayIcon();

  void showDashboard();
  void showAddons();
  void showLauncher();
  void showModules();
  void showMarkers();
  void showNetwork();
  void showThemes();
  void showSettings();
  void updateTrayMenu();
  void launchGW2ForUpdate(
      const QString &gw2Path = QString()); // Thin wrapper → UpdateManager

  // Core services
  DataService *m_dataService;
  AddonManager *m_addonManager;
  LaunchManager *m_launchManager;
  UpdateChecker *m_updateChecker;
  BlishModuleManager *m_blishManager;
  GW2APIClient *m_apiClient;
  UpdateManager *m_updateManager = nullptr;
  MarkerController *m_markerController;

  // UI
  QWidget *m_centralWidget;
  QStackedWidget *m_pages;
  QWidget *m_navBar;
  QLabel *m_statusLabel;
  LauncherWidget *m_launcherWidget;
  MarkerPackBrowser *m_markerBrowser;
  SettingsWidget *m_settingsWidget;
  NetworkWidget *m_networkWidget;

  // System tray
  QSystemTrayIcon *m_trayIcon;
  QMenu *m_trayMenu;
  bool m_forceQuit = false;
  QPushButton *m_minimizeToTrayBtn = nullptr; // Hidden when tray icon disabled

  // Nav buttons for highlighting
  QList<QPushButton *> m_navButtons;
  int m_currentNavIndex = 0;
  void updateNavButtonStyles();

  QString m_gw2Path;
  bool m_updateDialogShownThisSession =
      false; // Prevent multiple update dialogs per session

  // For frameless window dragging
  QPoint m_dragPos;
  bool m_dragging = false;

  // Global hotkeys (per-profile Focus/Minimize)
  HotkeyManager *m_profileHotkeyManager = nullptr;
  void registerProfileHotkeys();
  void onProfileHotkeyPressed(const QString &id);
};

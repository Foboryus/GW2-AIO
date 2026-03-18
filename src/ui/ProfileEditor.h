#pragma once

/**
 * @brief Profile Editor Dialog - Full settings like LaunchBuddy
 *
 * Now with iOS-style toggles and proper login fields!
 */

#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/LocalDatManager.h"
#include "core/ProfileManager.h"
#include "core/ServerManager.h"

class DataService;
class MarkerController;
#include "ui/ToggleSwitch.h"
#include "ui/WindowGridSelector.h"
#include "ui/profile/AddonsTabWidget.h"
#include "ui/profile/ArgumentsTabWidget.h"
#include "ui/profile/GeneralTabWidget.h"
#include "ui/profile/GraphicsTabWidget.h"
#include "ui/profile/HotkeysTabWidget.h"
#include "ui/profile/LoginTabWidget.h"
#include "ui/profile/MarkersTabWidget.h"
#include "ui/profile/NetworkTabWidget.h"
#include "ui/profile/WindowTabWidget.h"

// Common GW2 launch arguments with descriptions
struct LaunchArg {
  QString arg;
  QString desc;
  bool enabled;
};

class ProfileEditor : public QDialog {
  Q_OBJECT

public:
  explicit ProfileEditor(AccountProfile *profile,
                         ProfileManager *profileManager = nullptr,
                         ServerManager *serverManager = nullptr,
                         DataService *dataService = nullptr,
                         MarkerController *markerController = nullptr,
                         QWidget *parent = nullptr);

  AccountProfile getProfile() const { return m_profile; }

protected:
  // For frameless window dragging
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void reject() override;

private slots:
  void onAccept();
  void onApply();
  void markDirty();
  void markClean();
  void onTabChanged(int index);

private:
  void setupUI();
  void loadProfile();
  void saveProfile();

  QPoint m_dragPos;
  bool m_dragging = false;
  bool m_dirty = false;
  QLabel *m_saveIndicator = nullptr;
  QPushButton *m_applyBtn = nullptr;
  QStackedWidget *m_stack = nullptr;
  QList<QPushButton *> m_tabButtons;
  void switchToTab(int index);
  static void applyTabStyle(QPushButton *btn, bool active);
  bool m_graphicsLoaded = false;
  bool m_markersLoaded = false;

  // Tab factory methods - ALL REPLACED BY TAB WIDGETS
  // createGeneralTab() -> replaced by GeneralTabWidget
  // createLoginTab() -> replaced by LoginTabWidget
  // createArgumentsTab() -> replaced by ArgumentsTabWidget
  // createWindowTab() -> replaced by WindowTabWidget
  // createNetworkTab() -> replaced by NetworkTabWidget
  // createGraphicsTab() -> replaced by GraphicsTabWidget
  // createAddonsTab() -> replaced by AddonsTabWidget

  AccountProfile m_profile;
  ProfileManager *m_profileManager = nullptr;
  DataService *m_dataService = nullptr;

  // General tab widget
  GeneralTabWidget *m_generalTab = nullptr;

  // Arguments tab widget
  ArgumentsTabWidget *m_argumentsTab = nullptr;

  // Window tab widget
  WindowTabWidget *m_windowTab = nullptr;

  // Login tab widget
  LoginTabWidget *m_loginTab = nullptr;

  // Graphics tab widget
  GraphicsTabWidget *m_graphicsTab = nullptr;

  // Addons tab widget
  AddonsTabWidget *m_addonsTab = nullptr;

  // Network tab widget
  NetworkTabWidget *m_networkTab = nullptr;
  ServerManager *m_serverManager = nullptr;

  // Hotkeys tab widget
  HotkeysTabWidget *m_hotkeysTab = nullptr;

  // Markers tab widget
  MarkersTabWidget *m_markersTab = nullptr;
  MarkerController *m_markerController = nullptr;

  // Complete official GW2 arguments from wiki
  // Source: https://wiki.guildwars2.com/wiki/Command_line_arguments
  QList<LaunchArg> m_standardArgs = {
      // == System ==
      {"-32", "Force 32-bit mode", false},

      // == Login & Network ==
      {"-autologin", "Auto-login with saved credentials", false},
      {"-authsrv", "Custom login server (use with IP/DNS)", false},
      {"-clientport 443", "Use HTTPS port (avoids 6112 blocks)", false},
      {"-clientport 80", "Use HTTP port (avoids 6112 blocks)", false},
      {"-portal", "Custom portal/gate server (use with IP/DNS)", false},

      // == Display & Graphics ==
      {"-windowed", "Run in windowed mode", false},
      {"-forwardrenderer", "Forward rendering (may improve FPS)", false},
      {"-uispanallmonitors", "Span UI across all monitors", false},
      {"-useOldFov", "Use old field of view", false},
      {"-ignorecoherentgpucrash", "Ignore UI GPU crashes", false},

      // == Performance ==
      {"-mapLoadinfo", "Show map loading percentage", true},
      {"-fps", "Limit framerate (e.g. -fps 60)", false},
      {"-umbra gpu", "GPU occlusion culling", false},

      // == Audio ==
      {"-nomusic", "Disable music", false},
      {"-nosound", "Disable all sound", false},
      {"-mce", "Windows Media Center support", false},

      // == Multi-Boxing ==
      {"-shareArchive", "Allow multiple instances", false},
      {"-mumble", "Custom MumbleLink name", false},

      // == Files & Logging ==
      {"-bmp", "Screenshots as BMP", false},
      {"-log", "Enable Gw2.log logging", false},
      {"-diag", "Create NetworkDiag.log", false},
      {"-dat", "Custom Gw2.dat path", false},

      // == Maintenance ==
      {"-repair", "Repair game files", false},
      {"-verify", "Verify game archive", false},
      {"-image", "Download updates only", false},
      {"-prefreset", "Reset graphics settings", false},
      {"-nodelta", "Disable delta patching", false},
      {"-uninstall", "Show uninstall dialog", false},

      // == Provider ==
      {"-provider Portal", "ArenaNet account (Steam bypass)", false},
      {"-provider Steam", "Steam account", false},
      {"-provider Epic", "Epic Games account", false},

      // == UI ==
      {"-noui", "Hide UI (Ctrl+Shift+H)", false},
  };
};

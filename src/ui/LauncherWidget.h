#pragma once

class CredentialRefreshManager;
class MarkerController;

/**
 * @brief Launcher Widget - Full-featured launcher UI
 *
 * Provides:
 * - Profile management (add, edit, delete)
 * - Multi-boxing support
 * - Quick launch
 * - Per-account settings
 */

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include "core/DllInjector.h"
#include "core/GW2WindowWatcher.h"
#include "core/LaunchManager.h"
#include "core/ProfileManager.h"
#include "core/ServerManager.h"
#include "ui/ProfileEditor.h"
#include "ui/ToggleSwitch.h"

class DataService; // Forward declaration

class LauncherWidget : public QWidget {
  Q_OBJECT

public:
  explicit LauncherWidget(DataService *dataService,
                          LaunchManager *launchManager,
                          QWidget *parent = nullptr);

  void setGw2Path(const QString &path) {
    m_gw2Path = path;
    updateStatus();
  }

  void setServerManager(ServerManager *serverManager) {
    m_serverManager = serverManager;
  }

  void setMarkerController(MarkerController *markerController) {
    m_markerController = markerController;
  }

  ProfileManager *profileManager() { return m_profileManager; }

  // Call this when Launcher tab is shown or AIO relaunches
  // to refresh running badges (validates PIDs and updates UI)
  void refreshRunningStates();

  // Launch profile if not running, or focus its window if already running
  void launchOrFocusProfile(const QString &profileId);

private slots:
  void onAddProfile();
  void onEditProfile();
  void onDeleteProfile();
  void onCloneProfile();
  void onMoveProfileUp();
  void onMoveProfileDown();
  void onLaunchSelected();
  void onLaunchAll();
  void onCheckForUpdates();
  void onProfileDoubleClicked(QListWidgetItem *item);
  void onProfileContextMenu(const QPoint &pos);
  void onSettings();
  void onExportProfile();
  void onImportProfile();
  void updateProfileList();
  void updateStatus();

private:
  void setupUI();
  void applyStyle();
  AccountProfile *selectedProfile();

  /**
   * @brief Trigger-based wait: blocks until onGW2WindowDetected fires
   * for the given profile. Uses QEventLoop + profileWindowConfirmed signal.
   * No timers. GW2 window = Local.dat read + mutex created.
   */
  bool waitForWindowConfirm(const QString &profileId);

  DataService *m_dataService;
  LaunchManager *m_launchManager;
  ProfileManager *m_profileManager;
  DllInjector *m_dllInjector;
  ServerManager *m_serverManager = nullptr;
  MarkerController *m_markerController = nullptr;
  CredentialRefreshManager *m_credRefreshMgr = nullptr;
  QString m_gw2Path;

  // UI
  QListWidget *m_profileList;
  QPushButton *m_addBtn;
  QPushButton *m_editBtn;
  QPushButton *m_deleteBtn;
  QPushButton *m_cloneBtn;
  QPushButton *m_moveUpBtn;
  QPushButton *m_moveDownBtn;
  QPushButton *m_importBtn;
  QPushButton *m_exportBtn;
  QPushButton *m_launchBtn;
  QPushButton *m_launchAllBtn;
  QPushButton *m_settingsBtn;
  QLabel *m_statusLabel;
  LabeledToggle *m_multiBoxToggle;
  QLineEdit *m_argsEdit;

  // Permanent cancel container (hidden/shown, never removed from layout)
  QWidget *m_cancelContainer = nullptr;
  QLabel *m_cancelIcon = nullptr;
  QLabel *m_cancelText = nullptr;
  QPushButton *m_cancelBtn = nullptr;

  // Profile selection toggles and row widgets
  QMap<QString, ToggleSwitch *> m_profileToggles; // profile ID -> toggle
  QMap<QString, QWidget *> m_profileRows;         // profile ID -> row widget
  QList<QString> getSelectedProfileIds();

  // Serial launch helper: wait for profile PID to be confirmed (event-based, NO
  // TIMERS)
  bool waitForProfilePid(const QString &profileId);

  // Flag to prevent itemClicked from overriding direct toggle clicks
  bool m_toggleDirectlyClicked = false;

  // Multibox guard: returns true if launch should proceed, false to abort
  bool checkMultiboxGuard(int profileCount);

  // Pre-flight credential refresh: returns true if launch should proceed
  bool runPreFlightRefresh(const QList<AccountProfile> &profiles);
};

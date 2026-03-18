#include "MainWindow.h"
#include "core/AddonManager.h"
#include "core/BlishModuleManager.h"
#include "core/DataService.h"
#include "core/HotkeyManager.h"
#include "core/LocalDatManager.h"
#include "core/ThemeManager.h"
#include "core/UpdateManager.h"
#include "ui/AboutDialog.h"
#include "ui/LauncherWidget.h"
#include "ui/MarkerPackBrowser.h"
#include "ui/NetworkWidget.h"
#include "ui/SettingsWidget.h"
#include "ui/UIHelpers.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QScrollArea>
#include <QStandardPaths>
#include <QUrl>
#include <shellapi.h> // DragAcceptFiles, DragQueryFileW, DragFinish

#include <QPainter>
#include <QStyleOption>
#include <chrono>
#include <string>

/**
 * @brief QWidget subclass that renders QSS properties (border-radius, etc.)
 *
 * Per Qt docs, plain QWidget does NOT render stylesheet properties like
 * border-radius. This subclass adds the required paintEvent override.
 * See: https://doc.qt.io/qt-6/stylesheet-reference.html#qwidget-widget
 */
class StyledWidget : public QWidget {
protected:
  void paintEvent(QPaintEvent *) override {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
  }
};

MainWindow::MainWindow(DataService *dataService,
                       MarkerController *markerController, QWidget *parent)
    : QMainWindow(parent), m_dataService(dataService),
      m_markerController(markerController),
      m_addonManager(new AddonManager(this)),
      m_launchManager(new LaunchManager(this)),
      m_updateChecker(new UpdateChecker(this)),
      m_blishManager(new BlishModuleManager(this)),
      m_apiClient(new GW2APIClient(this)), m_trayIcon(nullptr) {

  // Inject DataService's LocalDatManager into LaunchManager
  m_launchManager->setLocalDatManager(m_dataService->localDatManager());

  // Note: symlink fallback warning removed — junction approach doesn't use
  // symlinks

  // Frameless window with rounded corners
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_AlwaysShowToolTips); // Show tooltips even when unfocused
  setMinimumSize(900, 905);

  setupUI();
  setupTrayIcon();

  // Enable Win32 shell drag-drop on elevated HWND
  // Qt's OLE-based setAcceptDrops doesn't work due to UIPI
  // (User Interface Privilege Isolation) blocking messages from
  // non-elevated Explorer to our requireAdministrator process.
  // IMPORTANT: Must be AFTER setupUI() to get the final native HWND.
  //
  // Strategy:
  // 1. RevokeDragDrop removes any OLE IDropTarget Qt registered internally.
  //    OLE targets override WM_DROPFILES — Windows won't send WM_DROPFILES
  //    while an OLE target is active on the HWND. Qt's setAcceptDrops(false)
  //    alone does NOT fully unregister OLE, so we must use Win32 API directly.
  // 2. DragAcceptFiles registers for legacy WM_DROPFILES messages.
  // 3. ChangeWindowMessageFilterEx punches through UIPI for the drop messages.
  HWND hwnd = (HWND)winId();
  RevokeDragDrop(hwnd);
  DragAcceptFiles(hwnd, TRUE);
  ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
  ChangeWindowMessageFilterEx(hwnd, 0x0049 /*WM_COPYGLOBALDATA*/, MSGFLT_ALLOW,
                              nullptr);

  // Connect to application focus change for badge refresh on window regain
  connect(qApp, &QApplication::focusWindowChanged, this,
          [this](QWindow *window) {
            Q_UNUSED(window);
            // Use isActiveWindow() for reliable detection
            if (isActiveWindow() && m_launcherWidget &&
                m_pages->currentIndex() == 2) {
              qInfo() << "MainWindow: Focus regained (focusWindowChanged) - "
                         "refreshing running states";
              m_launcherWidget->refreshRunningStates();
            }
          });

  // Load saved GW2 path and propagate to all managers
  QString savedPath = m_dataService->gw2Path();
  if (!savedPath.isEmpty()) {
    setGW2Path(savedPath);
  }

  // === Crash Detection: Check if GW2 crashed due to needing patch ===
  if (m_launchManager->checkForPatchCrash()) {
    QTimer::singleShot(1000, this, [this]() {
      // Styled crash dialog
      auto *crashDialog = new QDialog(this);
      crashDialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                                  Qt::FramelessWindowHint);
      crashDialog->setAttribute(Qt::WA_TranslucentBackground);
      crashDialog->setMinimumWidth(420);
      auto *ol = new QVBoxLayout(crashDialog);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      bg->setObjectName("crashBg");
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      ly->setSpacing(15);
      auto *tl = new QLabel("GW2 Update Required");
      UIHelpers::applyGoldTitleRole(tl);
      tl->setAlignment(Qt::AlignCenter);
      ly->addWidget(tl);
      auto *lb =
          new QLabel("GW2 crashed because it needs to be updated.\n\nWould you "
                     "like to launch GW2 now to download updates?");
      UIHelpers::applyPopupLabelRole(lb);
      lb->setAlignment(Qt::AlignCenter);
      lb->setWordWrap(true);
      ly->addWidget(lb);
      auto *btnLayout = new QHBoxLayout();
      btnLayout->setSpacing(12);
      auto *noBtn = new QPushButton("No");
      noBtn->setMinimumHeight(36);
      UIHelpers::applyCancelStyle(noBtn);
      connect(noBtn, &QPushButton::clicked, crashDialog, &QDialog::reject);
      btnLayout->addWidget(noBtn);
      auto *yesBtn = new QPushButton("Yes");
      yesBtn->setMinimumHeight(36);
      UIHelpers::applyPrimaryStyle(yesBtn);
      connect(yesBtn, &QPushButton::clicked, crashDialog, &QDialog::accept);
      btnLayout->addWidget(yesBtn);
      ly->addLayout(btnLayout);
      // Position dialog
      crashDialog->adjustSize();
      QScreen *screen = QGuiApplication::primaryScreen();
      if (screen) {
        QRect geom = screen->availableGeometry();
        int x = geom.x() + (geom.width() - crashDialog->width()) / 2;
        int y = geom.y() + geom.height() / 6;
        crashDialog->move(x, y);
      }
      if (crashDialog->exec() == QDialog::Accepted) {
        launchGW2ForUpdate();
      }
      crashDialog->deleteLater();
    });
  }

  // Note: 7-day reminder removed - we now have proactive API-based build
  // checking

  // === Build ID Sync on Successful Load ===
  // Delegate to UpdateManager — stores build ID + exe timestamp
  connect(m_launchManager, &LaunchManager::profileLoaded, this,
          [this](const QString &gw2Path) {
            if (m_updateManager) {
              m_updateManager->onProfileLoaded(gw2Path);
            }
          });

  // === Profile Crash During Launch Detection ===
  connect(
      m_launchManager, &LaunchManager::profileCrashDuringLaunch, this,
      [this](const QString &profileId, const QString &profileName,
             const QString &gw2Path, const QString &reason) {
        Q_UNUSED(profileId);

        qInfo() << "Profile crash during launch detected:" << profileName
                << "Reason:" << reason;

        // Create styled crash dialog with proper rounded corners
        QDialog *crashDialog = new QDialog(this);
        crashDialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                                    Qt::FramelessWindowHint);
        crashDialog->setAttribute(Qt::WA_TranslucentBackground);
        crashDialog->setMinimumWidth(420);

        // Outer layout for transparent dialog
        auto *outerLayout = new QVBoxLayout(crashDialog);
        outerLayout->setContentsMargins(0, 0, 0, 0);

        // Background container with gold border + rounded corners
        auto *bgContainer = new QWidget();
        UIHelpers::applyPopupBackgroundRole(bgContainer);
        outerLayout->addWidget(bgContainer);

        auto *layout = new QVBoxLayout(bgContainer);
        layout->setSpacing(20);
        layout->setContentsMargins(30, 30, 30, 30);

        // Header container: styled dark box with rounded borders (no
        // border)
        auto *headerContainer = new QWidget(bgContainer);
        UIHelpers::applyContainerRole(headerContainer);
        auto *headerLayout = new QHBoxLayout(headerContainer);
        headerLayout->setSpacing(12);
        headerLayout->setContentsMargins(16, 12, 16, 12);

        // Profile icon
        ProfileManager *pm = m_launcherWidget->profileManager();
        auto *iconLabel = new QLabel(headerContainer);
        QString iconPath = ":/icons/profile-game.svg"; // Default
        if (pm) {
          AccountProfile *profile = pm->profile(profileId);
          if (profile && !profile->icon.isEmpty() &&
              profile->icon.startsWith(":/icons/")) {
            iconPath = profile->icon;
          }
        }
        iconLabel->setPixmap(QIcon(iconPath).pixmap(32, 32));
        iconLabel->setFixedSize(36, 36);
        UIHelpers::applyTransparentRole(iconLabel);
        headerLayout->addWidget(iconLabel);

        // Message with profile name
        auto *messageLabel = new QLabel(headerContainer);
        QString message = QString("<b>%1 crashed during launch</b><br>%2")
                              .arg(profileName, reason);
        messageLabel->setText(message);
        messageLabel->setWordWrap(true);
        UIHelpers::applyLabelRole(messageLabel);
        messageLabel->setStyleSheet("background: transparent;");
        headerLayout->addWidget(messageLabel, 1);

        layout->addWidget(headerContainer);

        // Buttons row
        auto *buttonLayout = new QHBoxLayout();
        buttonLayout->setSpacing(15);

        // Check if update is needed
        bool updateNeeded = false;
        if (m_updateManager) {
          int localBuildId = m_updateManager->getPathBuildId(gw2Path);
          int remoteBuild = m_updateManager->remoteBuildId();
          updateNeeded = (remoteBuild != 0 && remoteBuild != localBuildId);
        }

        if (updateNeeded) {
          auto *updateBtn = new QPushButton("Update Now", crashDialog);
          updateBtn->setMinimumHeight(45);
          UIHelpers::applyActionStyle(updateBtn);
          connect(updateBtn, &QPushButton::clicked, crashDialog,
                  [this, crashDialog, gw2Path]() {
                    crashDialog->accept();
                    launchGW2ForUpdate(gw2Path);
                  });
          buttonLayout->addWidget(updateBtn);
        }

        auto *retryBtn = new QPushButton("Try Again", crashDialog);
        retryBtn->setMinimumHeight(45);
        UIHelpers::applyNeutralStyle(retryBtn);
        connect(retryBtn, &QPushButton::clicked, crashDialog, [crashDialog]() {
          crashDialog->done(1); // Return 1 for retry
        });
        buttonLayout->addWidget(retryBtn);

        auto *cancelBtn = new QPushButton("Cancel", crashDialog);
        cancelBtn->setMinimumHeight(45);
        UIHelpers::applyCancelStyle(cancelBtn);
        connect(cancelBtn, &QPushButton::clicked, crashDialog,
                &QDialog::reject);
        buttonLayout->addWidget(cancelBtn);

        layout->addLayout(buttonLayout);

        // Position dialog
        crashDialog->adjustSize();
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
          QRect geom = screen->availableGeometry();
          int x = geom.x() + (geom.width() - crashDialog->width()) / 2;
          int y = geom.y() + geom.height() / 6;
          crashDialog->move(x, y);
        }

        int result = crashDialog->exec();
        crashDialog->deleteLater();

        // Handle Try Again - re-launch the crashed profile
        if (result == 1) {
          ProfileManager *pm = m_launcherWidget->profileManager();
          if (pm) {
            AccountProfile *profilePtr = pm->profile(profileId);
            if (profilePtr) {
              // Pre-launch build check before retry
              QString effectivePath =
                  LaunchManager::getEffectiveGw2Path(*profilePtr, m_gw2Path);
              if (!checkGW2BuildBeforeLaunch(effectivePath)) {
                qInfo() << "Retry blocked - update needed for" << effectivePath;
                return;
              }

              qInfo() << "User clicked Try Again - re-launching profile:"
                      << profileName;
              m_launchManager->launchWithProfile(*profilePtr);
            }
          }
        }
      });

  // === UpdateManager Setup ===
  m_updateManager = m_dataService->updateManager();
  m_updateManager->setApiClient(m_apiClient);
  m_updateManager->setLaunchManager(m_launchManager);

  // Connect UpdateManager signals for UI dialogs
  connect(m_updateManager, &UpdateManager::updateRequired, this,
          [this](const QStringList &outdatedPaths) {
            m_updateDialogShownThisSession = true;

            // Build a message listing all outdated paths
            QString pathList;
            for (const QString &p : outdatedPaths) {
              pathList += QString("\u2022 %1<br>").arg(p);
            }

            // Create styled update dialog
            QDialog *updateDialog = new QDialog(this);
            updateDialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                                         Qt::FramelessWindowHint);
            updateDialog->setMinimumWidth(400);
            UIHelpers::applyDialogRole(updateDialog);

            auto *layout = new QVBoxLayout(updateDialog);
            layout->setSpacing(20);
            layout->setContentsMargins(30, 30, 30, 30);

            auto *messageContainer =
                UIHelpers::createMessageContainer(updateDialog);
            auto *messageLayout =
                qobject_cast<QVBoxLayout *>(messageContainer->layout());

            auto *messageLabel = UIHelpers::createLabel(
                messageContainer,
                "<b>GW2 Update Required</b><br><br>"
                "A new Guild Wars 2 update is available.<br><br>"
                "GW2 must be updated before launching profiles.<br>"
                "Click Update Now to launch GW2 for patching.");
            messageLabel->setAlignment(Qt::AlignCenter);
            messageLayout->addWidget(messageLabel);

            layout->addWidget(messageContainer);

            auto *updateBtn = new QPushButton("Update Now", updateDialog);
            updateBtn->setMinimumHeight(50);
            UIHelpers::applyCancelStyle(updateBtn);
            layout->addWidget(updateBtn);

            UIHelpers::centerDialog(updateDialog);

            // Use the first outdated path for the update launch
            QString firstPath =
                outdatedPaths.isEmpty() ? m_gw2Path : outdatedPaths.first();
            connect(updateBtn, &QPushButton::clicked, updateDialog,
                    [this, updateDialog, firstPath]() {
                      updateDialog->accept();
                      launchGW2ForUpdate(firstPath);
                    });

            updateDialog->exec();
            updateDialog->deleteLater();
          });

  connect(m_updateManager, &UpdateManager::updateComplete, this,
          [this](const QString &path) {
            qInfo() << "Update complete for path:" << path;
            m_updateDialogShownThisSession = false;
          });

  connect(m_updateManager, &UpdateManager::updateError, this,
          [this](const QString &message) {
            // Styled error dialog
            auto *d = new QDialog(this);
            d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
            d->setAttribute(Qt::WA_TranslucentBackground);
            d->setMinimumWidth(400);
            auto *ol = new QVBoxLayout(d);
            ol->setContentsMargins(0, 0, 0, 0);
            auto *bg = new QWidget();
            bg->setObjectName("updateErrBg");
            UIHelpers::applyPopupBackgroundRole(bg);
            ol->addWidget(bg);
            auto *ly = new QVBoxLayout(bg);
            ly->setContentsMargins(20, 20, 20, 20);
            auto *tl = new QLabel("Update Check Failed");
            UIHelpers::applyGoldTitleRole(tl);
            tl->setAlignment(Qt::AlignCenter);
            ly->addWidget(tl);
            auto *lb = new QLabel(message);
            UIHelpers::applyPopupLabelRole(lb);
            lb->setAlignment(Qt::AlignCenter);
            lb->setWordWrap(true);
            ly->addWidget(lb);
            auto *ok = new QPushButton("OK");
            ok->setMinimumHeight(36);
            UIHelpers::applyPrimaryStyle(ok);
            connect(ok, &QPushButton::clicked, d, &QDialog::accept);
            ly->addWidget(ok);
            UIHelpers::centerDialog(d);
            d->exec();
            d->deleteLater();
          });

  // Handle stale paths (GW2 install moved/deleted)
  connect(m_updateManager, &UpdateManager::stalePathsRemoved, this,
          [this](const QStringList &removedPaths) {
            QString pathList;
            for (const QString &p : removedPaths) {
              pathList += QString("\u2022 %1<br>").arg(p);
            }

            // Styled info dialog
            auto *d = new QDialog(this);
            d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
            d->setAttribute(Qt::WA_TranslucentBackground);
            d->setMinimumWidth(420);
            auto *ol = new QVBoxLayout(d);
            ol->setContentsMargins(0, 0, 0, 0);
            auto *bg = new QWidget();
            UIHelpers::applyPopupBackgroundRole(bg);
            ol->addWidget(bg);
            auto *ly = new QVBoxLayout(bg);
            ly->setContentsMargins(20, 20, 20, 20);
            ly->setSpacing(12);
            auto *tl = new QLabel("Install Path Removed");
            UIHelpers::applyGoldTitleRole(tl);
            tl->setAlignment(Qt::AlignCenter);
            ly->addWidget(tl);
            auto *lb = new QLabel(
                "The following GW2 install paths were removed because "
                "Gw2-64.exe was no longer found:<br><br>" +
                pathList +
                "<br>If GW2 was moved to a new location, please update "
                "your profile paths.");
            UIHelpers::applyPopupLabelRole(lb);
            lb->setAlignment(Qt::AlignCenter);
            lb->setWordWrap(true);
            ly->addWidget(lb);
            auto *ok = new QPushButton("OK");
            ok->setMinimumHeight(36);
            UIHelpers::applyPrimaryStyle(ok);
            connect(ok, &QPushButton::clicked, d, &QDialog::accept);
            ly->addWidget(ok);
            UIHelpers::centerDialog(d);
            d->exec();
            d->deleteLater();
          });

  // Check for GW2 updates on startup (non-blocking, fail-safe)
  QTimer::singleShot(2000, m_updateManager, &UpdateManager::checkForUpdates);
}

// === Update Logic (thin wrappers to UpdateManager) ===

void MainWindow::launchGW2ForUpdate(const QString &gw2Path) {
  QString updatePath = gw2Path.isEmpty() ? m_gw2Path : gw2Path;

  if (!m_updateManager) {
    return;
  }

  // Create styled monitoring dialog that blocks until update completes
  QDialog *updateDialog = new QDialog(this);
  updateDialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                               Qt::FramelessWindowHint);
  updateDialog->setAttribute(Qt::WA_TranslucentBackground);
  updateDialog->setMinimumWidth(420);

  auto *outerLayout = new QVBoxLayout(updateDialog);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  auto *bgContainer = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bgContainer);
  outerLayout->addWidget(bgContainer);

  auto *layout = new QVBoxLayout(bgContainer);
  layout->setSpacing(20);
  layout->setContentsMargins(30, 30, 30, 30);

  auto *messageContainer = UIHelpers::createMessageContainer(bgContainer);
  auto *messageLayout = qobject_cast<QVBoxLayout *>(messageContainer->layout());

  auto *messageLabel = UIHelpers::createLabel(
      messageContainer,
      "<b>GW2 Update In Progress</b><br><br>"
      "GW2 has been launched to check for updates.<br><br>"
      "Please wait for patching to complete,<br>"
      "then <b>close GW2</b> to continue.<br><br>"
      "<i>This window will close automatically when GW2 exits.</i>");
  messageLabel->setAlignment(Qt::AlignCenter);
  messageLayout->addWidget(messageLabel);

  auto *statusLabel =
      new QLabel("Waiting for GW2 to close...", messageContainer);
  UIHelpers::applySecondaryRole(statusLabel);
  statusLabel->setAlignment(Qt::AlignCenter);
  messageLayout->addWidget(statusLabel);

  layout->addWidget(messageContainer);

  auto *cancelBtn = new QPushButton("Cancel");
  UIHelpers::applyCancelStyle(cancelBtn);
  cancelBtn->setMinimumHeight(36);
  connect(cancelBtn, &QPushButton::clicked, updateDialog, &QDialog::reject);
  layout->addWidget(cancelBtn);

  UIHelpers::centerDialog(updateDialog);

  // Auto-close dialog when update completes or errors out
  // Use QPointer to guard against use-after-free if user cancelled
  QPointer<QDialog> weakDialog(updateDialog);

  QMetaObject::Connection completeConn =
      connect(m_updateManager, &UpdateManager::updateComplete, updateDialog,
              [weakDialog](const QString &) {
                if (weakDialog)
                  weakDialog->accept();
              });

  QMetaObject::Connection errorConn =
      connect(m_updateManager, &UpdateManager::updateError, updateDialog,
              [weakDialog](const QString &) {
                if (weakDialog)
                  weakDialog->reject();
              });

  // Launch GW2 for update (spawns background thread for monitoring)
  m_updateManager->launchUpdateForPath(updatePath, this);

  // Block until GW2 exits or user cancels
  updateDialog->exec();
  updateDialog->deleteLater();

  // Disconnect to avoid stale connections
  disconnect(completeConn);
  disconnect(errorConn);

  // Reset session flag so profile launches are no longer blocked
  m_updateDialogShownThisSession = false;
}

bool MainWindow::checkGW2BuildBeforeLaunch(const QString &gw2Path) {
  QString checkPath = gw2Path.isEmpty() ? m_gw2Path : gw2Path;

  if (!m_updateManager) {
    return true; // No update manager, allow launch
  }

  bool canLaunch = m_updateManager->canLaunch(checkPath);
  return canLaunch;
}

void MainWindow::setGW2Path(const QString &path) {
  // Handle both file path (Gw2-64.exe) and directory path
  QString dirPath = path;
  QFileInfo info(path);
  if (info.isFile()) {
    dirPath = info.absolutePath();
  }

  m_gw2Path = dirPath;
  m_addonManager->setGw2Path(dirPath);
  m_launchManager->setGw2Path(dirPath);

  // Update LauncherWidget with the GW2 path
  if (m_launcherWidget) {
    m_launcherWidget->setGw2Path(dirPath);
  }

  if (dirPath.isEmpty()) {
    m_statusLabel->setText("⚠ GW2 installation not found");
  } else {
    m_statusLabel->setText(QString("✓ GW2: %1").arg(dirPath));
  }
}

void MainWindow::setupUI() {
  m_centralWidget = new QWidget(this);
  UIHelpers::applyTransparentRole(m_centralWidget);
  setCentralWidget(m_centralWidget);

  // Outer layout (transparent)
  auto *outerLayout = new QVBoxLayout(m_centralWidget);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);

  // Background container for rounded corners (since window is translucent)
  auto *bgContainer = new StyledWidget();
  UIHelpers::applyWindowBackgroundRole(bgContainer);
  auto *bgLayout = new QVBoxLayout(bgContainer);
  bgLayout->setContentsMargins(8, 8, 8, 8); // Gray padding around content
  bgLayout->setSpacing(8);
  outerLayout->addWidget(bgContainer);

  // === Custom Title Bar (matches ProfileEditor style) ===
  auto *titleBar = new QWidget();
  UIHelpers::applyRole(titleBar, "titleBar");
  auto *titleBarLayout = new QHBoxLayout(titleBar);
  titleBarLayout->setContentsMargins(12, 10, 10, 10);

  // App icon (use SVG app icon)
  auto *appIcon = new QLabel();
  UIHelpers::setThemedPixmap(appIcon, "app-icon", 24);
  appIcon->setStyleSheet("background: transparent; border: none;");
  titleBarLayout->addWidget(appIcon);

  // Title (larger like ProfileEditor)
  auto *titleLabel = new QLabel("GW2 AIO Manager");
  UIHelpers::applyGoldColorRole(titleLabel);
  titleLabel->setStyleSheet(
      QString("font-size: %1px; font-weight: bold; margin-left: 4px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeTitle));
  titleBarLayout->addWidget(titleLabel);

  titleBarLayout->addStretch();

  // Normal minimize button (minimize window)
  auto *minimizeWindowBtn = new QPushButton();
  UIHelpers::setThemedIcon(minimizeWindowBtn, "minus-gold");
  minimizeWindowBtn->setIconSize(QSize(14, 14));
  minimizeWindowBtn->setFixedSize(28, 28);
  minimizeWindowBtn->setToolTip("Minimize");
  UIHelpers::applyRole(minimizeWindowBtn, "titleMinimize");
  connect(minimizeWindowBtn, &QPushButton::clicked, this,
          [this]() { showMinimized(); });
  titleBarLayout->addWidget(minimizeWindowBtn);

  // Minimize to tray button (stored as member for visibility control)
  m_minimizeToTrayBtn = new QPushButton();
  UIHelpers::setThemedIcon(m_minimizeToTrayBtn, "chevron-down");
  m_minimizeToTrayBtn->setIconSize(QSize(14, 14));
  m_minimizeToTrayBtn->setFixedSize(28, 28);
  m_minimizeToTrayBtn->setToolTip("Minimize to Tray");
  UIHelpers::applyRole(m_minimizeToTrayBtn, "titleTray");
  connect(m_minimizeToTrayBtn, &QPushButton::clicked, this, [this]() {
    hide(); // Minimize to tray
  });
  // Set initial visibility based on tray icon setting
  {
    m_minimizeToTrayBtn->setVisible(m_dataService->showTrayIcon());
  }
  titleBarLayout->addWidget(m_minimizeToTrayBtn);

  // Close button
  auto *closeBtn = new QPushButton();
  UIHelpers::setThemedIcon(closeBtn, "x");
  closeBtn->setIconSize(QSize(14, 14));
  closeBtn->setFixedSize(28, 28);
  closeBtn->setToolTip("Close");
  UIHelpers::applyRole(closeBtn, "titleClose");
  connect(closeBtn, &QPushButton::clicked, this, [this]() {
    m_forceQuit = true;
    close();
  });
  titleBarLayout->addWidget(closeBtn);

  bgLayout->addWidget(titleBar);

  // === Main Content Area ===
  auto *contentWidget = new QWidget();
  contentWidget->setAttribute(Qt::WA_TranslucentBackground);
  auto *mainLayout = new QHBoxLayout(contentWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(
      4); // Gap between nav and content (window bg peeks through)
  bgLayout->addWidget(contentWidget);

  // Left navigation bar
  m_navBar = new QWidget();
  m_navBar->setFixedWidth(200);
  m_navBar->setObjectName("navBar");

  auto *navLayout = new QVBoxLayout(m_navBar);
  navLayout->setSpacing(5);
  navLayout->setContentsMargins(10, 15, 10, 20);

  // Navigation buttons with themed SVG icons
  auto *btnDashboard = new QPushButton("Dashboard");
  UIHelpers::setThemedIcon(btnDashboard, "home");
  auto *btnAddons = new QPushButton("Addons");
  UIHelpers::setThemedIcon(btnAddons, "box");
  auto *btnLauncher = new QPushButton("Launcher");
  UIHelpers::setThemedIcon(btnLauncher, "play");
  auto *btnModules = new QPushButton("Blish Modules");
  UIHelpers::setThemedIcon(btnModules, "grid");
  auto *btnMarkers = new QPushButton("Markers");
  UIHelpers::setThemedIcon(btnMarkers, "map-pin");

  auto *btnNetwork = new QPushButton("Global Network");
  UIHelpers::setThemedIcon(btnNetwork, "globe");
  auto *btnThemes = new QPushButton("Themes");
  UIHelpers::setThemedIcon(btnThemes, "palette");
  auto *btnSettings = new QPushButton("Settings");
  UIHelpers::setThemedIcon(btnSettings, "settings");

  btnDashboard->setObjectName("navButton");
  btnAddons->setObjectName("navButton");
  btnLauncher->setObjectName("navButton");
  btnModules->setObjectName("navButton");
  btnMarkers->setObjectName("navButton");
  btnNetwork->setObjectName("navButton");
  btnThemes->setObjectName("navButton");
  btnSettings->setObjectName("navButton");

  m_navButtons.append(btnDashboard);
  m_navButtons.append(btnAddons);
  m_navButtons.append(btnLauncher);
  m_navButtons.append(btnModules);
  m_navButtons.append(btnMarkers);
  m_navButtons.append(btnNetwork);
  m_navButtons.append(btnThemes);
  m_navButtons.append(btnSettings);

  navLayout->addWidget(btnDashboard);
  navLayout->addWidget(btnAddons);
  navLayout->addWidget(btnLauncher);
  navLayout->addWidget(btnModules);
  navLayout->addWidget(btnMarkers);
  navLayout->addWidget(btnNetwork);
  navLayout->addWidget(btnThemes);
  navLayout->addStretch();
  navLayout->addWidget(btnSettings);

  // About button (opens dialog, not a page)
  auto *btnAbout = new QPushButton("About");
  UIHelpers::setThemedIcon(btnAbout, "book");
  btnAbout->setObjectName("navButton");
  connect(btnAbout, &QPushButton::clicked, [this]() {
    AboutDialog dialog(this);
    dialog.exec();
  });
  navLayout->addWidget(btnAbout);

  // Status label at bottom
  m_statusLabel = new QLabel("Initializing...");
  m_statusLabel->setObjectName("statusLabel");
  m_statusLabel->setWordWrap(true);
  navLayout->addWidget(m_statusLabel);

  mainLayout->addWidget(m_navBar);

  // Pages container — themed via QSS (#pages rule in 02_Navigation.h)
  m_pages = new QStackedWidget();
  m_pages->setObjectName("pages");

  // Create page placeholders
  auto createPage = [](const QString &title,
                       const QString &iconName = QString()) {
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *header = UIHelpers::createPageHeader(page, title, iconName);
    layout->addWidget(header);

    return page;
  };

  // Dashboard page
  auto *dashboardPage = createPage("Overview", "lightbulb");
  auto *dashLayout = qobject_cast<QVBoxLayout *>(dashboardPage->layout());

  auto *statsGroup = new QGroupBox("Quick Stats");
  auto *statsLayout = new QHBoxLayout(statsGroup);
  statsLayout->addWidget(new QLabel("Installed Addons: 0"));
  statsLayout->addWidget(new QLabel("Updates Available: 0"));
  statsLayout->addWidget(new QLabel("Blish Modules: 0"));
  dashLayout->addWidget(statsGroup);

  auto *launchBtn = new QPushButton("Launch GW2");
  UIHelpers::setThemedIcon(launchBtn, "play-circle");
  UIHelpers::applyPrimaryStyle(launchBtn);
  launchBtn->setFixedHeight(50);
  dashLayout->addWidget(launchBtn);
  dashLayout->addStretch();

  m_pages->addWidget(dashboardPage);

  // Addons page
  auto *addonsPage = createPage("Addon Manager", "layers");
  auto *addonsLayout = qobject_cast<QVBoxLayout *>(addonsPage->layout());

  auto *addonsList = new QListWidget();
  addonsList->setObjectName("addonList");
  addonsLayout->addWidget(addonsList);

  auto *updateAllBtn = new QPushButton("Update All");
  UIHelpers::setThemedIcon(updateAllBtn, "refresh");
  UIHelpers::applyNeutralStyle(updateAllBtn);
  addonsLayout->addWidget(updateAllBtn);

  m_pages->addWidget(addonsPage);

  // Launcher page - Use full-featured LauncherWidget
  m_launcherWidget = new LauncherWidget(m_dataService, m_launchManager, this);
  m_pages->addWidget(m_launcherWidget);

  // Connect profile changes to update tray menu
  connect(m_launcherWidget->profileManager(), &ProfileManager::profilesChanged,
          this, &MainWindow::updateTrayMenu);

  // Global hotkeys for per-profile Focus/Minimize
  m_profileHotkeyManager = new HotkeyManager(this);
  connect(m_profileHotkeyManager, &HotkeyManager::hotkeyPressed, this,
          &MainWindow::onProfileHotkeyPressed);
  connect(m_dataService, &DataService::profilesChanged, this,
          &MainWindow::registerProfileHotkeys);
  connect(m_dataService, &DataService::profileUpdated, this,
          [this](const QString &) { registerProfileHotkeys(); });
  registerProfileHotkeys();

  // Notify user if profiles were restored from backup (auto-recovery)
  connect(m_launcherWidget->profileManager(),
          &ProfileManager::profilesRestoredFromBackup, this, [this]() {
            // Show styled popup informing user of auto-recovery
            auto *d = UIHelpers::createStyledDialog(this, 420);
            auto *layout = qobject_cast<QVBoxLayout *>(d->layout());

            auto *titleLabel = new QLabel("Profiles Restored");
            UIHelpers::applyGoldTitleRole(titleLabel);
            titleLabel->setAlignment(Qt::AlignCenter);
            layout->addWidget(titleLabel);

            auto *msgLabel =
                new QLabel("Profile data was restored from backup.\n\n"
                           "Your most recent changes may have been lost.\n"
                           "This can happen after an unexpected shutdown.");
            UIHelpers::applyPopupLabelRole(msgLabel);
            msgLabel->setWordWrap(true);
            msgLabel->setAlignment(Qt::AlignCenter);
            layout->addWidget(msgLabel);

            auto *okBtn = new QPushButton("OK");
            UIHelpers::applyPrimaryStyle(okBtn);
            connect(okBtn, &QPushButton::clicked, d, &QDialog::accept);
            layout->addWidget(okBtn);

            UIHelpers::centerDialog(d);
            d->exec();
            d->deleteLater();
          });

  // Modules page
  auto *modulesPage = createPage("Blish-HUD Modules", "book");
  auto *modulesLayout = qobject_cast<QVBoxLayout *>(modulesPage->layout());

  auto *modulesInfo =
      new QLabel("Blish-HUD modules require .NET 6+ Runtime\n"
                 "Place .bhm files in: %AppData%/GW2AIO/BlishModules/");
  modulesInfo->setWordWrap(true);
  UIHelpers::applyHintRole(modulesInfo);
  modulesInfo->setStyleSheet(
      QString("padding: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.paddingNormal));
  modulesLayout->addWidget(modulesInfo);

  auto *modulesList = new QListWidget();
  modulesList->setObjectName("moduleList");
  modulesList->addItem("No modules installed");
  modulesLayout->addWidget(modulesList);

  auto *installModuleBtn = new QPushButton("Browse Module Repository...");
  UIHelpers::setThemedIcon(installModuleBtn, "download");
  UIHelpers::applyNeutralStyle(installModuleBtn);
  modulesLayout->addWidget(installModuleBtn);

  auto *openFolderBtn = new QPushButton("Open Modules Folder");
  UIHelpers::setThemedIcon(openFolderBtn, "folder");
  UIHelpers::applyNeutralStyle(openFolderBtn);
  modulesLayout->addWidget(openFolderBtn);
  modulesLayout->addStretch();

  m_pages->addWidget(modulesPage);

  // Markers page
  m_markerBrowser =
      new MarkerPackBrowser(m_dataService, m_markerController, this);
  m_pages->addWidget(m_markerBrowser);

  // Network page
  m_networkWidget = new NetworkWidget();
  auto *networkScroll = new QScrollArea();
  networkScroll->setWidget(m_networkWidget);
  networkScroll->setWidgetResizable(true);
  networkScroll->setFrameShape(QFrame::NoFrame);
  m_pages->addWidget(networkScroll);

  // Connect network settings to launcher
  m_launcherWidget->setServerManager(m_networkWidget->serverManager());
  m_launcherWidget->setMarkerController(m_markerController);

  // Connect "Apply to All Profiles" button
  connect(m_networkWidget, &NetworkWidget::applyToAllProfiles, [this]() {
    ProfileManager *pm = m_launcherWidget->profileManager();
    if (pm) {
      for (const AccountProfile &p : pm->profiles()) {
        AccountProfile updated = p;
        updated.networkMode = NetworkMode::UseGlobal;
        updated.customNetworkServer.clear();

        // Strip conflicting network args from Arguments tab
        QMutableStringListIterator it(updated.arguments);
        while (it.hasNext()) {
          QString arg = it.next();
          if (arg.startsWith("-authsrv") || arg.startsWith("-portal") ||
              arg.startsWith("-assetsrv") || arg.startsWith("-clientport")) {
            it.remove();
          }
        }

        pm->updateProfile(updated);
      }
    }
  });

  // Theme page
  {
    auto *themePage = createPage("Theme Selector", "eye");
    auto *themeLayout = qobject_cast<QVBoxLayout *>(themePage->layout());

    auto *hintLabel = new QLabel("Choose a visual theme for the application.");
    UIHelpers::applyHintRole(hintLabel);
    themeLayout->addWidget(hintLabel);

    // Theme preset buttons
    auto themes = ThemeManager::builtinThemes();
    for (int i = 0; i < themes.size(); ++i) {
      auto bt = themes[i];
      auto *btn = new QPushButton(ThemeManager::themeName(bt));
      UIHelpers::setThemedIcon(btn, "palette");
      UIHelpers::applyNeutralStyle(btn);
      btn->setFixedHeight(44);
      connect(btn, &QPushButton::clicked, this, [this, bt, i]() {
        ThemeManager::instance().setBuiltinTheme(bt);
        m_dataService->setSelectedTheme(i);
      });
      themeLayout->addWidget(btn);
    }

    themeLayout->addStretch();
    m_pages->addWidget(themePage);
  }

  // Settings page
  m_settingsWidget = new SettingsWidget(m_dataService);
  auto *settingsScroll = new QScrollArea();
  settingsScroll->setWidget(m_settingsWidget);
  settingsScroll->setWidgetResizable(true);
  settingsScroll->setFrameShape(QFrame::NoFrame);
  m_pages->addWidget(settingsScroll);

  // Connect settings to update GW2 path
  connect(m_settingsWidget, &SettingsWidget::gw2PathChanged, this,
          &MainWindow::setGW2Path);

  // Connect settings to update tray icon visibility immediately
  connect(m_settingsWidget, &SettingsWidget::settingsChanged, this, [this]() {
    bool showTray = m_dataService->showTrayIcon();
    m_trayIcon->setVisible(showTray);

    // Update minimize-to-tray button visibility
    if (m_minimizeToTrayBtn) {
      m_minimizeToTrayBtn->setVisible(showTray);
    }
  });

  mainLayout->addWidget(m_pages);

  // Connect navigation
  connect(btnDashboard, &QPushButton::clicked, this,
          &MainWindow::showDashboard);
  connect(btnAddons, &QPushButton::clicked, this, &MainWindow::showAddons);
  connect(btnLauncher, &QPushButton::clicked, this, &MainWindow::showLauncher);
  connect(btnModules, &QPushButton::clicked, this, &MainWindow::showModules);
  connect(btnMarkers, &QPushButton::clicked, this, &MainWindow::showMarkers);
  connect(btnNetwork, &QPushButton::clicked, this, &MainWindow::showNetwork);
  connect(btnThemes, &QPushButton::clicked, this, &MainWindow::showThemes);
  connect(btnSettings, &QPushButton::clicked, this, &MainWindow::showSettings);

  // Connect launch button on Dashboard
  connect(launchBtn, &QPushButton::clicked, this, [this]() {
    if (m_gw2Path.isEmpty()) {
      // Styled warning dialog
      auto *d = new QDialog(this);
      d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
      d->setAttribute(Qt::WA_TranslucentBackground);
      d->setMinimumWidth(420);
      auto *ol = new QVBoxLayout(d);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      bg->setObjectName("gw2NotFoundBg");
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      auto *tl = new QLabel("GW2 Not Found");
      UIHelpers::applyGoldTitleRole(tl);
      tl->setAlignment(Qt::AlignCenter);
      ly->addWidget(tl);
      auto *lb =
          new QLabel("Guild Wars 2 installation path is not set.\n\nPlease run "
                     "the Setup Wizard or set the path in Settings.");
      UIHelpers::applyPopupLabelRole(lb);
      lb->setAlignment(Qt::AlignCenter);
      lb->setWordWrap(true);
      ly->addWidget(lb);
      auto *ok = new QPushButton("OK");
      ok->setMinimumHeight(36);
      UIHelpers::applyPrimaryStyle(ok);
      connect(ok, &QPushButton::clicked, d, &QDialog::accept);
      ly->addWidget(ok);
      UIHelpers::centerDialog(d);
      d->exec();
      d->deleteLater();
      return;
    }
    LaunchProfile profile;
    profile.name = "Default";
    QProcess *proc = m_launchManager->launchGW2(profile);
    if (!proc) {
      // Styled warning dialog
      auto *d = new QDialog(this);
      d->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
      d->setAttribute(Qt::WA_TranslucentBackground);
      d->setMinimumWidth(420);
      auto *ol = new QVBoxLayout(d);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      bg->setObjectName("launchFailBg");
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      auto *tl = new QLabel("Launch Failed");
      UIHelpers::applyGoldTitleRole(tl);
      tl->setAlignment(Qt::AlignCenter);
      ly->addWidget(tl);
      auto *lb = new QLabel("Failed to launch Guild Wars 2.\n\nCheck that "
                            "Gw2-64.exe exists in:\n" +
                            m_gw2Path);
      UIHelpers::applyPopupLabelRole(lb);
      lb->setAlignment(Qt::AlignCenter);
      lb->setWordWrap(true);
      ly->addWidget(lb);
      auto *ok = new QPushButton("OK");
      ok->setMinimumHeight(36);
      UIHelpers::applyPrimaryStyle(ok);
      connect(ok, &QPushButton::clicked, d, &QDialog::accept);
      ly->addWidget(ok);
      UIHelpers::centerDialog(d);
      d->exec();
      d->deleteLater();
    }
  });

  // (Launcher page now uses LauncherWidget which handles its own connections)

  // Connect Modules page buttons
  connect(openFolderBtn, &QPushButton::clicked, []() {
    QString modulesPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/BlishModules";
    QDir().mkpath(modulesPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(modulesPath));
  });

  connect(installModuleBtn, &QPushButton::clicked, []() {
    QDesktopServices::openUrl(QUrl("https://blishhud.com/modules"));
  });

  // Initialize nav button highlighting (Dashboard selected by default)
  m_currentNavIndex = 0;
  updateNavButtonStyles();
}

// applyGW2Theme() removed — all styling now handled by ThemeManager global QSS

void MainWindow::showDashboard() {
  m_pages->setCurrentIndex(0);
  m_currentNavIndex = 0;
  updateNavButtonStyles();
}
void MainWindow::showAddons() {
  m_pages->setCurrentIndex(1);
  m_currentNavIndex = 1;
  updateNavButtonStyles();
}
void MainWindow::showLauncher() {
  m_pages->setCurrentIndex(2);
  m_currentNavIndex = 2;
  updateNavButtonStyles();

  // Refresh running badges when showing Launcher tab
  if (m_launcherWidget) {
    m_launcherWidget->refreshRunningStates();
  }
}
void MainWindow::showModules() {
  m_pages->setCurrentIndex(3);
  m_currentNavIndex = 3;
  updateNavButtonStyles();
}
void MainWindow::showMarkers() {
  m_pages->setCurrentIndex(4);
  m_currentNavIndex = 4;
  updateNavButtonStyles();
}
void MainWindow::showNetwork() {
  m_pages->setCurrentIndex(5);
  m_currentNavIndex = 5;
  updateNavButtonStyles();
}
void MainWindow::showThemes() {
  QElapsedTimer t;
  t.start();
  m_pages->setCurrentIndex(6);
  qint64 switchMs = t.elapsed();
  m_currentNavIndex = 6;
  updateNavButtonStyles();
  qDebug() << "[Perf] showThemes: pageSwitch=" << switchMs
           << "ms total=" << t.elapsed() << "ms";
}

void MainWindow::showSettings() {
  m_pages->setCurrentIndex(7);
  m_currentNavIndex = 7;
  updateNavButtonStyles();
}

void MainWindow::updateNavButtonStyles() {
  QElapsedTimer t;
  t.start();
  for (int i = 0; i < m_navButtons.size(); ++i) {
    if (i == m_currentNavIndex) {
      // Selected: gold text, subtle background
      UIHelpers::applyNavActiveStyle(m_navButtons[i]);
    } else {
      // Unselected: gray text, transparent background
      UIHelpers::applyNavInactiveStyle(m_navButtons[i]);
    }
  }
  qint64 ms = t.elapsed();
  if (ms > 10) {
    qDebug() << "[Perf] updateNavButtonStyles took" << ms << "ms";
  }
}

void MainWindow::setupTrayIcon() {
  m_trayIcon = new QSystemTrayIcon(QIcon(":/icons/app-icon.svg"), this);
  m_trayIcon->setToolTip("GW2 AIO Manager");

  // Create tray menu — styled dynamically in updateTrayMenu()
  m_trayMenu = new QMenu(this);
  updateTrayMenu();
  m_trayIcon->setContextMenu(m_trayMenu);

  // Re-apply tray menu theme when user switches themes
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
          &MainWindow::updateTrayMenu);

  // Double-click to show
  connect(m_trayIcon, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
              show();
              raise();
              activateWindow();
            }
          });

  // Show tray icon on app start if enabled in settings
  if (m_dataService->showTrayIcon()) {
    m_trayIcon->show();
  }
}

void MainWindow::updateTrayMenu() {
  if (!m_trayMenu)
    return;

  m_trayMenu->clear();

  // Add profile quick-launch actions at top
  if (m_launcherWidget) {
    ProfileManager *pm = m_launcherWidget->profileManager();
    if (pm && !pm->profiles().isEmpty()) {
      for (const AccountProfile &profile : pm->profiles()) {
        QString profileId = profile.id;
        QString displayName =
            profile.nickname.isEmpty() ? "Profile" : profile.nickname;

        // Use the profile's custom icon
        // Match logic from LauncherWidget (profile-game.svg default, :/icons/
        // check)
        QString iconPath =
            profile.icon.isEmpty() ? ":/icons/profile-game.svg" : profile.icon;
        if (!iconPath.startsWith(":/icons/")) {
          iconPath = ":/icons/profile-game.svg"; // Legacy emoji fallback
        }
        QIcon icon(iconPath);

        auto *action = m_trayMenu->addAction(icon, displayName);
        connect(action, &QAction::triggered, this, [this, profileId]() {
          // Option C: Launch if not running, focus if already running
          // Note: Dialog is parentless and always-on-top, no need to show main
          // window
          if (m_launcherWidget) {
            m_launcherWidget->launchOrFocusProfile(profileId);
          }
        });
      }

      m_trayMenu->addSeparator();
    }
  }

  // Show AIO action
  auto *showAction =
      m_trayMenu->addAction(QIcon(":/icons/app-icon.svg"), "Show AIO");
  connect(showAction, &QAction::triggered, this, [this]() {
    show();
    raise();
    activateWindow();
  });

  m_trayMenu->addSeparator();

  // Quit action
  auto *quitAction = m_trayMenu->addAction(UIHelpers::themedIcon("x"), "Quit");
  connect(quitAction, &QAction::triggered, this, [this]() {
    m_forceQuit = true; // Bypasses minimize-to-tray in closeEvent
    close();            // Triggers closeEvent which will accept
  });

  const auto &t = ThemeManager::instance().activeTheme();
  m_trayMenu->setStyleSheet(
      QString(
          "QMenu { background: %1; border: 1px solid %2; padding: 8px 0px; "
          "min-width: 180px; }"
          "QMenu::item { background: transparent; color: %3; padding: 8px "
          "24px 8px 12px; }"
          "QMenu::item:selected { background: %4; }"
          "QMenu::icon { margin-left: 8px; margin-right: 6px; }"
          "QMenu::separator { height: 1px; background: %5; margin: 6px 12px; }")
          .arg(t.colors.windowSurface, t.widgets.groupboxBorder,
               t.colors.textPrimary, t.widgets.comboboxDropdownBg,
               t.widgets.groupboxBorder));
}

void MainWindow::closeEvent(QCloseEvent *event) {
  // If force quit was requested (from tray menu), actually close
  if (m_forceQuit) {
    event->accept();
    qApp->quit(); // Ensure app terminates
    return;
  }

  // Check if minimize to tray is enabled in settings
  bool showTray = m_dataService->showTrayIcon();

  if (showTray) {
    // Minimize to tray instead of closing
    hide();
    m_trayIcon->show();
    m_trayIcon->showMessage("GW2 AIO Manager", "Minimized to tray.",
                            QSystemTrayIcon::Information, 1500);
    event->ignore();
  } else {
    // No tray - just close
    event->accept();
  }
}

// ============================================================================
// Drag & Drop — Win32 WM_DROPFILES via nativeEvent (UIPI bypass)
// ============================================================================

static const int kMarkersPageIndex = 4;

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
                             qintptr *result) {
  MSG *msg = static_cast<MSG *>(message);
  if (msg->message == WM_DROPFILES) {
    qInfo() << "DIAG: WM_DROPFILES received in nativeEvent";
    HDROP hDrop = reinterpret_cast<HDROP>(msg->wParam);
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

    QList<QUrl> urls;
    for (UINT i = 0; i < fileCount; ++i) {
      wchar_t path[MAX_PATH];
      if (DragQueryFileW(hDrop, i, path, MAX_PATH) > 0) {
        QString filePath = QString::fromWCharArray(path);
        if (filePath.endsWith(".taco", Qt::CaseInsensitive) ||
            filePath.endsWith(".zip", Qt::CaseInsensitive) ||
            filePath.endsWith(".aiomt", Qt::CaseInsensitive)) {
          urls.append(QUrl::fromLocalFile(filePath));
        }
      }
    }
    DragFinish(hDrop);

    if (!urls.isEmpty() && m_pages->currentIndex() == kMarkersPageIndex &&
        m_markerBrowser) {
      m_markerBrowser->handleDroppedFiles(urls);
    }

    *result = 0;
    return true; // We handled WM_DROPFILES
  }
  return QMainWindow::nativeEvent(eventType, message, result);
}

bool MainWindow::event(QEvent *event) {
  // Detect when window gains focus/activation
  if (event->type() == QEvent::ActivationChange) {
    if (isActiveWindow()) {
      // Refresh launcher running states if on launcher page
      if (m_launcherWidget && m_pages->currentIndex() == 2) {
        qInfo() << "MainWindow: Window activated - refreshing running states";
        m_launcherWidget->refreshRunningStates();
      }

      // Re-check for GW2 updates on window focus (non-blocking)
      if (m_updateManager) {
        m_updateManager->checkForUpdates();
      }
    }
  }
  return QMainWindow::event(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    // Only allow drag from title bar area (top 50 pixels)
    if (event->position().y() < 50) {
      m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
      m_dragging = true;
      event->accept();
    } else {
      m_dragging = false;
    }
  }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPos);
    event->accept();
  }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = false;
  }
  QMainWindow::mouseReleaseEvent(event);
}

// === Global Hotkeys ===

void MainWindow::registerProfileHotkeys() {
  m_profileHotkeyManager->unregisterAll();

  const auto profiles = m_dataService->profiles();

  for (const auto &profile : profiles) {
    if (!profile.hotkeyFocus.isEmpty()) {
      QString id = "focus:" + profile.id;
      if (!m_profileHotkeyManager->registerHotkey(id, profile.hotkeyFocus)) {
        qWarning() << "Failed to register focus hotkey" << profile.hotkeyFocus
                   << "for profile" << profile.nickname;
      }
    }
    if (!profile.hotkeyMinimize.isEmpty()) {
      QString id = "minimize:" + profile.id;
      if (!m_profileHotkeyManager->registerHotkey(id, profile.hotkeyMinimize)) {
        qWarning() << "Failed to register minimize hotkey"
                   << profile.hotkeyMinimize << "for profile"
                   << profile.nickname;
      }
    }
  }
}

void MainWindow::onProfileHotkeyPressed(const QString &id) {
  // ID format: "focus:profileId" or "minimize:profileId"
  int colonPos = id.indexOf(':');
  if (colonPos == -1)
    return;

  QString action = id.left(colonPos);
  QString profileId = id.mid(colonPos + 1);

  if (action == "focus") {
    if (m_dataService->isProfileRunning(profileId)) {
      m_dataService->bringProfileWindowToFocus(profileId);
    } else {
      qInfo() << "Focus hotkey pressed but profile not running:" << profileId;
    }
  } else if (action == "minimize") {
    if (m_dataService->isProfileRunning(profileId)) {
      m_dataService->minimizeProfileWindow(profileId);
    } else {
      qInfo() << "Minimize hotkey pressed but profile not running:"
              << profileId;
    }
  }
}

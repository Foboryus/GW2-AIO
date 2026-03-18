#include "LauncherWidget.h"

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

#include "UIHelpers.h"
#include "core/CredentialRefreshManager.h"
#include "core/DataService.h"
#include "core/ThemeManager.h"
#include "core/UpdateManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

LauncherWidget::LauncherWidget(DataService *dataService,
                               LaunchManager *launchManager, QWidget *parent)
    : QWidget(parent), m_dataService(dataService),
      m_launchManager(launchManager) {
  // ProfileManager is now owned by DataService — use its instance
  m_profileManager = m_dataService->profileManager();
  m_dllInjector = new DllInjector(this);

  // Create credential refresh manager (after m_launchManager is set)
  if (m_launchManager && m_launchManager->localDatManager()) {
    m_credRefreshMgr = new CredentialRefreshManager(
        m_launchManager, m_launchManager->localDatManager(), this);
  }

  setupUI();
  applyStyle();
  updateProfileList();
  updateStatus();

  // Connect signals
  connect(m_launchManager, &LaunchManager::gw2Launched, this,
          &LauncherWidget::updateStatus);
  connect(m_launchManager, &LaunchManager::gw2Exited, this,
          [this](qint64 pid, int exitCode) {
            Q_UNUSED(pid);
            Q_UNUSED(exitCode);
            updateStatus();
            updateProfileList(); // Refresh to clear stale running badges
          });
  connect(m_profileManager, &ProfileManager::profilesChanged, this,
          &LauncherWidget::updateProfileList);

  // Connect profile running state tracking
  connect(
      m_launchManager, &LaunchManager::profileLaunched, this,
      [this](const QString &profileId, qint64 pid) {
        qInfo() << "LauncherWidget: Profile launched - storing running state:"
                << profileId << "PID:" << pid;
        QString mumbleName =
            m_launchManager->mumbleLinkNameForProfile(profileId);
        m_profileManager->setProfileRunning(profileId, pid, mumbleName);
        updateProfileList(); // Refresh to show "Running" badge
      });

  connect(m_profileManager, &ProfileManager::profileRunningStateChanged, this,
          [this](const QString &profileId, bool running) {
            Q_UNUSED(profileId);
            Q_UNUSED(running);
            updateProfileList(); // Refresh to show/hide "Running" badge
          });

  // Track credential freshness and build verification when GW2 reaches
  // character select (LOADED signal from helper DLL). This confirms:
  // - The profile's .dat credentials are fresh (for credential refresh check)
  // - The profile's Local.dat has been through the current GW2 build
  connect(m_launchManager, &LaunchManager::profileCharacterSelectReached, this,
          [this](const QString &profileId) {
            auto *p = m_profileManager->profile(profileId);
            if (p) {
              p->lastLoginTime = QDateTime::currentDateTime();

              // Mark this profile as verified at the current GW2 build
              auto *um = m_dataService->updateManager();
              if (um && um->remoteBuildId() > 0) {
                p->lastVerifiedBuild = um->remoteBuildId();
              }

              m_profileManager->updateProfile(*p);
              qInfo() << "Profile verified at build"
                      << p->lastVerifiedBuild << "for:" << p->nickname;
            }
          });
}

void LauncherWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);

  // Header
  auto *header =
      UIHelpers::createPageHeader(this, "Launch Profiles", "play-circle");
  auto *headerLayout = qobject_cast<QHBoxLayout *>(header->layout());

  m_settingsBtn = new QPushButton("");
  UIHelpers::setThemedIcon(m_settingsBtn, "settings");
  UIHelpers::applyNeutralStyle(m_settingsBtn);
  m_settingsBtn->setMaximumWidth(35);
  m_settingsBtn->setToolTip("Change GW2 installation path");
  connect(m_settingsBtn, &QPushButton::clicked, this,
          &LauncherWidget::onSettings);
  headerLayout->addWidget(m_settingsBtn);

  mainLayout->addWidget(header);

  // Profiles section
  auto *profilesGroup = new QGroupBox("Launch Profiles");
  UIHelpers::applyGroupBoxRole(profilesGroup);
  auto *profilesLayout = new QVBoxLayout(profilesGroup);

  // Selection helper buttons
  auto *selectionLayout = new QHBoxLayout();
  auto *selectAllBtn = new QPushButton("All");
  UIHelpers::setThemedIcon(selectAllBtn, "check-square");
  UIHelpers::applyNeutralStyle(selectAllBtn);
  selectAllBtn->setMaximumWidth(90);
  connect(selectAllBtn, &QPushButton::clicked, [this]() {
    for (auto *toggle : m_profileToggles) {
      toggle->setChecked(true);
    }
  });
  selectionLayout->addWidget(selectAllBtn);

  auto *selectNoneBtn = new QPushButton("Clear");
  UIHelpers::setThemedIcon(selectNoneBtn, "x-circle");
  UIHelpers::applyCancelStyle(selectNoneBtn);
  selectNoneBtn->setMaximumWidth(100);
  connect(selectNoneBtn, &QPushButton::clicked, [this]() {
    for (auto *toggle : m_profileToggles) {
      toggle->setChecked(false);
    }
  });
  selectionLayout->addWidget(selectNoneBtn);

  selectionLayout->addStretch();
  auto *selectionHint =
      new QLabel("Toggle profiles to launch multiple at once");
  UIHelpers::applyHintRole(selectionHint);
  selectionLayout->addWidget(selectionHint);
  profilesLayout->addLayout(selectionLayout);

  m_profileList = new QListWidget();
  m_profileList->setSelectionMode(QAbstractItemView::NoSelection);
  m_profileList->setMinimumHeight(150);
  connect(m_profileList, &QListWidget::itemDoubleClicked, this,
          &LauncherWidget::onProfileDoubleClicked);

  // Clicking a profile row = exclusive toggle (turn off others, turn on this
  // one) BUT skip if the user directly clicked the toggle (allow additive
  // behavior)
  connect(m_profileList, &QListWidget::itemClicked,
          [this](QListWidgetItem *item) {
            // Skip if toggle was directly clicked
            if (m_toggleDirectlyClicked) {
              m_toggleDirectlyClicked = false;
              return;
            }

            if (!item)
              return;
            QString clickedId = item->data(Qt::UserRole).toString();

            // Turn off all other toggles, turn on the clicked one
            // Also directly update row widget styles since signals may not fire
            // correctly
            for (auto it = m_profileToggles.begin();
                 it != m_profileToggles.end(); ++it) {
              QString profileId = it.key();
              ToggleSwitch *toggle = it.value();
              QWidget *rowWidget = m_profileRows.value(profileId);

              if (profileId == clickedId) {
                toggle->setChecked(true);
                // No extra styling - toggle state is the visual indicator
              } else {
                toggle->setChecked(false);
                // No extra cleanup needed
              }
            }
          });

  profilesLayout->addWidget(m_profileList);

  // Profile buttons - apply explicit styles to prevent loss during layout
  // reflow
  auto *profileBtnsLayout = new QHBoxLayout();
  m_addBtn = new QPushButton("Add");
  UIHelpers::setThemedIcon(m_addBtn, "plus");
  m_editBtn = new QPushButton("Edit");
  UIHelpers::setThemedIcon(m_editBtn, "edit");
  m_cloneBtn = new QPushButton("Clone");
  UIHelpers::setThemedIcon(m_cloneBtn, "copy");
  m_deleteBtn = new QPushButton("Delete");
  UIHelpers::setThemedIcon(m_deleteBtn, "trash");
  m_importBtn = new QPushButton("Import");
  UIHelpers::setThemedIcon(m_importBtn, "download");
  m_exportBtn = new QPushButton("Export");
  UIHelpers::setThemedIcon(m_exportBtn, "upload");
  m_moveUpBtn = new QPushButton("");
  UIHelpers::setThemedIcon(m_moveUpBtn, "chevron-up");
  m_moveDownBtn = new QPushButton("");
  UIHelpers::setThemedIcon(m_moveDownBtn, "chevron-down");

  // Apply explicit styles to prevent loss during layout changes
  UIHelpers::applyNeutralStyle(m_addBtn);
  UIHelpers::applyNeutralStyle(m_editBtn);
  UIHelpers::applyNeutralStyle(m_cloneBtn);
  UIHelpers::applyCancelStyle(m_deleteBtn);
  UIHelpers::applyNeutralStyle(m_importBtn);
  UIHelpers::applyNeutralStyle(m_exportBtn);
  UIHelpers::applyNeutralStyle(m_moveUpBtn);
  UIHelpers::applyNeutralStyle(m_moveDownBtn);

  m_moveUpBtn->setMaximumWidth(35);
  m_moveDownBtn->setMaximumWidth(35);

  connect(m_addBtn, &QPushButton::clicked, this, &LauncherWidget::onAddProfile);
  connect(m_editBtn, &QPushButton::clicked, this,
          &LauncherWidget::onEditProfile);
  connect(m_cloneBtn, &QPushButton::clicked, this,
          &LauncherWidget::onCloneProfile);
  connect(m_deleteBtn, &QPushButton::clicked, this,
          &LauncherWidget::onDeleteProfile);
  connect(m_importBtn, &QPushButton::clicked, this,
          &LauncherWidget::onImportProfile);
  connect(m_exportBtn, &QPushButton::clicked, this,
          &LauncherWidget::onExportProfile);
  connect(m_moveUpBtn, &QPushButton::clicked, this,
          &LauncherWidget::onMoveProfileUp);
  connect(m_moveDownBtn, &QPushButton::clicked, this,
          &LauncherWidget::onMoveProfileDown);

  profileBtnsLayout->addWidget(m_addBtn);
  profileBtnsLayout->addWidget(m_editBtn);
  profileBtnsLayout->addWidget(m_cloneBtn);
  profileBtnsLayout->addWidget(m_deleteBtn);
  profileBtnsLayout->addWidget(m_importBtn);
  profileBtnsLayout->addWidget(m_exportBtn);
  profileBtnsLayout->addStretch();
  profileBtnsLayout->addWidget(m_moveUpBtn);
  profileBtnsLayout->addWidget(m_moveDownBtn);
  profilesLayout->addLayout(profileBtnsLayout);

  mainLayout->addWidget(profilesGroup);

  // Arguments section
  auto *argsGroup = new QGroupBox("Extra Launch Arguments");
  UIHelpers::applyGroupBoxRole(argsGroup);
  auto *argsLayout = new QVBoxLayout(argsGroup);

  m_argsEdit = new QLineEdit();
  m_argsEdit->setPlaceholderText("-maploadinfo -autologin");
  UIHelpers::applyInputFieldRole(m_argsEdit);
  argsLayout->addWidget(m_argsEdit);

  auto *argsHelpLabel =
      new QLabel("Common: -maploadinfo, -autologin, -windowed, -dx11");
  UIHelpers::applyHintRole(argsHelpLabel);
  argsLayout->addWidget(argsHelpLabel);

  mainLayout->addWidget(argsGroup);

  // Multi-box option - using visible toggle instead of checkbox
  m_multiBoxToggle =
      new LabeledToggle("Multi-Box Mode (close mutex between launches)");
  m_multiBoxToggle->setChecked(true); // Default ON
  m_multiBoxToggle->setToolTip(
      "Enables launching multiple GW2 instances by closing the mutex between "
      "each launch.\nRequired for running multiple accounts simultaneously.");
  mainLayout->addWidget(m_multiBoxToggle);
  // Sync toggle state → LaunchManager (controls -shareArchive and mutex)
  connect(m_multiBoxToggle, &LabeledToggle::toggled, m_launchManager,
          &LaunchManager::setMultiBoxEnabled);

  // Launch buttons
  auto *launchLayout = new QHBoxLayout();

  m_launchBtn = new QPushButton("Launch Selected");
  UIHelpers::setThemedIcon(m_launchBtn, "play-circle");
  m_launchBtn->setMinimumHeight(50);
  UIHelpers::applyNeutralStyle(m_launchBtn);
  connect(m_launchBtn, &QPushButton::clicked, this,
          &LauncherWidget::onLaunchSelected);

  m_launchAllBtn = new QPushButton("Launch All");
  UIHelpers::setThemedIcon(m_launchAllBtn, "play");
  m_launchAllBtn->setMinimumHeight(50);
  UIHelpers::applyNeutralStyle(m_launchAllBtn);
  connect(m_launchAllBtn, &QPushButton::clicked, this,
          &LauncherWidget::onLaunchAll);

  launchLayout->addWidget(m_launchBtn, 2);
  launchLayout->addWidget(m_launchAllBtn, 1);
  mainLayout->addLayout(launchLayout);

  // Preview command button
  auto *previewBtn = new QPushButton("Preview Launch Command");
  UIHelpers::setThemedIcon(previewBtn, "eye");
  UIHelpers::applyNeutralStyle(previewBtn);
  connect(previewBtn, &QPushButton::clicked, [this]() {
    // Get selected profiles (toggled ON), or fall back to list selection
    QList<QString> selectedIds = getSelectedProfileIds();
    AccountProfile *profile = nullptr;
    if (!selectedIds.isEmpty()) {
      profile = m_profileManager->profile(selectedIds.first());
    } else {
      profile = selectedProfile();
    }

    if (!profile) {
      // Styled "No profile selected" dialog
      auto *dialog = UIHelpers::createStyledDialog(
          const_cast<LauncherWidget *>(this), 350);

      // Match ProfileEditor structure exactly
      auto *outerLayout = new QVBoxLayout(dialog);
      outerLayout->setContentsMargins(0, 0, 0, 0);

      // Background container with gold border + rounded corners
      auto *bgContainer = new QWidget();
      bgContainer->setObjectName("popupBg");
      UIHelpers::applyPopupBackgroundRole(bgContainer);
      auto *layout = new QVBoxLayout(bgContainer);
      layout->setContentsMargins(20, 20, 20, 20);
      outerLayout->addWidget(bgContainer);

      auto *container = UIHelpers::createMessageContainer(bgContainer);
      auto *containerLayout = qobject_cast<QVBoxLayout *>(container->layout());
      auto *label = UIHelpers::createLabel(container, "No profile selected.");
      label->setAlignment(Qt::AlignCenter);
      containerLayout->addWidget(label);
      layout->addWidget(container);

      auto *okBtn = new QPushButton("OK");
      UIHelpers::applyConfirmStyle(okBtn);
      connect(okBtn, &QPushButton::clicked, dialog, &QDialog::accept);
      layout->addWidget(okBtn, 0, Qt::AlignCenter);

      UIHelpers::centerDialog(dialog);
      dialog->exec();
      dialog->deleteLater();
      return;
    }

    LaunchProfile lp = profile->toLaunchProfile();

    // Add extra args from text box
    QString extraArgs = m_argsEdit->text().trimmed();
    if (!extraArgs.isEmpty()) {
      lp.arguments.append(extraArgs.split(" ", Qt::SkipEmptyParts));
    }

    // Mirror runtime args that launchGW2() would add (honest preview)
    if (m_launchManager && m_launchManager->multiBoxEnabled() &&
        !lp.arguments.contains("-shareArchive")) {
      lp.arguments.append("-shareArchive");
    }

    QString exePath = m_gw2Path + "/Gw2-64.exe";
    QString fullCmd = QString("\"%1\" %2").arg(exePath, lp.arguments.join(" "));

    // Styled "Launch Command Preview" dialog
    auto *dialog =
        UIHelpers::createStyledDialog(const_cast<LauncherWidget *>(this), 600);

    // Match ProfileEditor structure exactly
    auto *outerLayout = new QVBoxLayout(dialog);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Background container with gold border + rounded corners
    auto *bgContainer = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bgContainer);
    auto *layout = new QVBoxLayout(bgContainer);
    layout->setContentsMargins(20, 20, 20, 20);
    outerLayout->addWidget(bgContainer);

    // Title
    auto *titleLabel =
        UIHelpers::createLabel(bgContainer, "Launch Command Preview", 16);
    UIHelpers::applyGoldTitleRole(titleLabel);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // Content container (no border)
    auto *container = new QWidget(bgContainer);
    UIHelpers::applyContainerRole(container);
    auto *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(15, 15, 15, 15);
    containerLayout->setSpacing(12);

    auto *cmdLabel = new QLabel("Command:", container);
    UIHelpers::applyHintRole(cmdLabel);
    containerLayout->addWidget(cmdLabel);

    auto *cmdText = new QLabel(fullCmd, container);
    UIHelpers::applySuccessColorRole(cmdText);
    cmdText->setStyleSheet(
        QString("font-family: monospace; padding: 8px; "
                "background: %1; border-radius: 4px;")
            .arg(ThemeManager::instance().activeTheme().colors.containerBg));
    cmdText->setWordWrap(true);
    cmdText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    containerLayout->addWidget(cmdText);

    auto *argsLabel = new QLabel("Arguments:", container);
    UIHelpers::applyHintRole(argsLabel);
    containerLayout->addWidget(argsLabel);

    QString argsList =
        lp.arguments.isEmpty() ? "(none)" : lp.arguments.join("\n");
    auto *argsText = new QLabel(argsList, container);
    UIHelpers::applySecondaryRole(argsText);
    argsText->setStyleSheet(
        QString("font-family: monospace; padding: 8px; "
                "background: %1; border-radius: 4px;")
            .arg(ThemeManager::instance().activeTheme().colors.containerBg));
    containerLayout->addWidget(argsText);

    auto *noteLabel = new QLabel("Note: Network settings from the Network tab "
                                 "are applied separately if configured.",
                                 container);
    UIHelpers::applyHintRole(noteLabel);
    noteLabel->setWordWrap(true);
    containerLayout->addWidget(noteLabel);

    layout->addWidget(container);

    auto *okBtn = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(okBtn);
    connect(okBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(okBtn, 0, Qt::AlignCenter);

    UIHelpers::centerDialog(dialog);
    dialog->exec();
    dialog->deleteLater();
  });
  mainLayout->addWidget(previewBtn);

  // Check for Updates button
  auto *patchBtn = new QPushButton("Check for Updates (Patch)");
  UIHelpers::setThemedIcon(patchBtn, "download");
  UIHelpers::applyConfirmStyle(patchBtn);
  patchBtn->setToolTip(
      "Launch all selected profiles without multibox arguments to allow "
      "patching.\nUseful after GW2 updates or for new installations.");
  connect(patchBtn, &QPushButton::clicked, this,
          &LauncherWidget::onCheckForUpdates);
  mainLayout->addWidget(patchBtn);

  // Status
  m_statusLabel = new QLabel();
  UIHelpers::applyStatusRole(m_statusLabel);
  mainLayout->addWidget(m_statusLabel);

  // Permanent cancel container (always in layout, hidden until needed)
  m_cancelContainer = new QWidget();
  m_cancelContainer->setObjectName("cancelOverlay");
  m_cancelContainer->setStyleSheet(
      QString("#cancelOverlay { background-color: %1; border: none; "
              "border-radius: 8px; padding: 10px; }")
          .arg(ThemeManager::instance().activeTheme().colors.windowBg));
  m_cancelContainer->setFixedHeight(50); // Fixed height prevents layout shifts
  auto *cancelLayout = new QHBoxLayout(m_cancelContainer);
  cancelLayout->setSpacing(12);
  cancelLayout->setContentsMargins(15, 10, 15, 10);

  m_cancelIcon = new QLabel();
  m_cancelIcon->setFixedSize(28, 28);
  m_cancelIcon->setAlignment(Qt::AlignCenter);
  m_cancelIcon->setStyleSheet("background: transparent; border: none;");
  cancelLayout->addWidget(m_cancelIcon);

  m_cancelText = new QLabel();
  UIHelpers::applyLabelRole(m_cancelText);
  m_cancelText->setStyleSheet("border: none;");
  cancelLayout->addWidget(m_cancelText);
  cancelLayout->addStretch();

  m_cancelBtn = new QPushButton("Cancel");
  UIHelpers::applyCancelStyle(m_cancelBtn);
  cancelLayout->addWidget(m_cancelBtn);

  m_cancelContainer->hide(); // Start hidden
  mainLayout->addWidget(m_cancelContainer);

  mainLayout->addStretch();
}

void LauncherWidget::applyStyle() {
  // Styles come from:
  //  - Global QSS template (04_DataWidgets.h for QListWidget)
  //  - Per-button roles via UIHelpers (applyNeutralStyle, applyCancelStyle
  //  etc.)
  // No widget-level setStyleSheet — it would block theme cascade.
}

void LauncherWidget::updateProfileList() {
  // Save checked toggle states before rebuild
  QSet<QString> checkedIds;
  for (auto it = m_profileToggles.constBegin();
       it != m_profileToggles.constEnd(); ++it) {
    if (it.value()->isChecked())
      checkedIds.insert(it.key());
  }

  m_profileList->clear();
  m_profileToggles.clear();
  m_profileRows.clear();

  const QList<AccountProfile> &profiles = m_profileManager->profiles();

  for (const auto &profile : profiles) {
    auto *item = new QListWidgetItem();
    item->setData(Qt::UserRole, profile.id);
    item->setSizeHint(QSize(0, 40));
    m_profileList->addItem(item);

    // Create row widget with toggle
    auto *rowWidget = new QWidget();
    auto *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(8, 4, 8, 4);

    // Toggle for selection
    auto *toggle = new ToggleSwitch();
    m_profileToggles[profile.id] = toggle;
    rowLayout->addWidget(toggle);

    // Profile icon (SVG or default)
    auto *iconLabel = new QLabel();
    QString iconPath =
        profile.icon.isEmpty() ? ":/icons/profile-game.svg" : profile.icon;
    // Check if it's an SVG path (new format) or old emoji
    if (iconPath.startsWith(":/icons/")) {
      iconLabel->setPixmap(QIcon(iconPath).pixmap(20, 20));
    } else {
      // Legacy emoji support - just show default icon
      UIHelpers::setThemedPixmap(iconLabel, "profile-game", 20);
    }
    iconLabel->setFixedSize(24, 24);
    rowLayout->addWidget(iconLabel);

    // Profile name
    QString text = profile.nickname;
    if (profile.accountProvider == AccountProvider::Steam) {
      text += " (Steam)";
    } else if (profile.accountProvider == AccountProvider::Epic) {
      text += " (Epic)";
    }

    auto *label = new QLabel(text);
    bool isRunning = m_profileManager->isProfileRunning(profile.id);
    if (isRunning) {
      UIHelpers::applySecondaryRole(label);
      label->setStyleSheet(
          QString("font-size: %1px;")
              .arg(
                  ThemeManager::instance().activeTheme().layout.fontSizeNormal -
                  1)); // Slightly smaller for running profiles
    } else {
      UIHelpers::applyLabelRole(label);
    }
    rowLayout->addWidget(label, 1);

    // Running badge (shown when profile is running)
    if (isRunning) {
      auto *runningLabel = new QLabel("RUNNING");
      UIHelpers::applyBadgeRole(runningLabel);
      runningLabel->setToolTip(
          QString("This profile is already running. Launching again will focus "
                  "open game.\nPID: %1")
              .arg(m_profileManager->getProfilePid(profile.id)));
      rowLayout->addWidget(runningLabel);
    }

    // === Status Icons (right side) ===
    // Fast tooltip delay for all status icons
    const int tooltipDelay = 150;

    // 1. Login status icon (always shown)
    auto *loginIcon = new QLabel();
    if (profile.accountProvider == AccountProvider::Steam) {
      // Steam profiles authenticate via Steam - no local login needed
      UIHelpers::setThemedPixmap(loginIcon, "steam", 16);
      loginIcon->setToolTip("Steam account - authenticates via Steam");
    } else if (profile.accountProvider == AccountProvider::Epic) {
      // Epic profiles authenticate via Epic - no local login needed
      UIHelpers::setThemedPixmap(loginIcon, "epic", 16);
      loginIcon->setToolTip("Epic account - authenticates via Epic Games");
    } else if (!profile.localDatPath.isEmpty()) {
      UIHelpers::setThemedPixmap(loginIcon, "check-circle", 16);
      loginIcon->setToolTip("Login credentials saved");
    } else {
      UIHelpers::setThemedPixmap(loginIcon, "alert-yellow", 16);
      loginIcon->setToolTip("No login saved - will need to enter credentials");
    }
    loginIcon->setFixedSize(20, 20);
    rowLayout->addWidget(loginIcon);

    // 2. Arguments icon (blue terminal - shown when args exist)
    if (!profile.arguments.isEmpty()) {
      auto *argsIcon = new QLabel();
      UIHelpers::setThemedPixmap(argsIcon, "status-args", 16);
      argsIcon->setToolTip(QString("%1 launch argument%2")
                               .arg(profile.arguments.size())
                               .arg(profile.arguments.size() > 1 ? "s" : ""));
      argsIcon->setFixedSize(20, 20);
      rowLayout->addWidget(argsIcon);
    }

    // 3. Window position icon (purple layout - shown when enabled)
    if (profile.useCustomWindow) {
      auto *windowIcon = new QLabel();
      UIHelpers::setThemedPixmap(windowIcon, "status-window", 16);
      windowIcon->setToolTip("Window will auto-position after launch");
      windowIcon->setFixedSize(20, 20);
      rowLayout->addWidget(windowIcon);
    }

    // 4. GFX icon (teal monitor - shown when saved AND will be applied)
    if (profile.useCustomGfx && !profile.gfxSettingsPath.isEmpty()) {
      auto *gfxIcon = new QLabel();
      UIHelpers::setThemedPixmap(gfxIcon, "status-gfx", 16);
      gfxIcon->setToolTip("Custom graphics settings will be applied");
      gfxIcon->setFixedSize(20, 20);
      rowLayout->addWidget(gfxIcon);
    }

    // 5. Addons icon (orange wrench - shown when DLLs exist)
    if (!profile.injectedDlls.isEmpty()) {
      auto *addonsIcon = new QLabel();
      UIHelpers::setThemedPixmap(addonsIcon, "status-addons", 16);
      addonsIcon->setToolTip(
          QString("%1 addon%2 will be injected")
              .arg(profile.injectedDlls.size())
              .arg(profile.injectedDlls.size() > 1 ? "s" : ""));
      addonsIcon->setFixedSize(20, 20);
      rowLayout->addWidget(addonsIcon);
    }

    // 6. Network icon (globe - shown for Global or Custom modes, not
    // UseDefault)
    if (profile.networkMode == NetworkMode::UseGlobal) {
      // Dark orange globe - using global settings
      auto *networkIcon = new QLabel();
      UIHelpers::setThemedPixmap(networkIcon, "status-network-global", 16);
      networkIcon->setToolTip("Using global network settings");
      networkIcon->setFixedSize(20, 20);
      rowLayout->addWidget(networkIcon);
    } else if (profile.networkMode == NetworkMode::Custom &&
               !profile.customNetworkServer.isEmpty()) {
      // Cyan globe - using custom server
      auto *networkIcon = new QLabel();
      UIHelpers::setThemedPixmap(networkIcon, "status-network-custom", 16);
      networkIcon->setToolTip(
          QString("Custom server: %1").arg(profile.customNetworkServer));
      networkIcon->setFixedSize(20, 20);
      rowLayout->addWidget(networkIcon);
    }
    // UseDefault mode = no icon (clean, no custom networking)

    // 7. Last login time (gray small text, right-aligned)
    if (profile.lastLoginTime.isValid()) {
      auto *lastLoginLabel =
          new QLabel(profile.lastLoginTime.toString("dd/MMM/yyyy hh:mm"));
      UIHelpers::applyHintRole(lastLoginLabel);
      lastLoginLabel->setStyleSheet(
          QString("font-size: %1px; padding-left: %2px;")
              .arg(ThemeManager::instance().activeTheme().layout.fontSizeBadge)
              .arg(
                  ThemeManager::instance().activeTheme().layout.paddingNormal));
      lastLoginLabel->setToolTip(
          "Last successful login: " +
          profile.lastLoginTime.toString("dddd, MMMM d, yyyy h:mm:ss AP"));
      rowLayout->addWidget(lastLoginLabel);
    }

    // Note: Running badge is already added above (near profile name)

    // Set flag when toggle is directly clicked (before itemClicked fires)
    connect(toggle, &ToggleSwitch::clicked,
            [this]() { m_toggleDirectlyClicked = true; });

    // Note: No extra row styling - the toggle itself is the visual indicator

    m_profileList->setItemWidget(item, rowWidget);
    m_profileRows[profile.id] = rowWidget; // Store for direct style updates

    // Restore toggle state from before rebuild
    if (checkedIds.contains(profile.id)) {
      toggle->setChecked(true);
    }
  }

  if (profiles.isEmpty()) {
    auto *item = new QListWidgetItem("No profiles - click Add to create one");
    m_profileList->addItem(item);
  }
}

QList<QString> LauncherWidget::getSelectedProfileIds() {
  QList<QString> selected;
  for (auto it = m_profileToggles.begin(); it != m_profileToggles.end(); ++it) {
    if (it.value()->isChecked()) {
      selected.append(it.key());
    }
  }
  return selected;
}

void LauncherWidget::updateStatus() {
  int count = m_profileManager->runningProfiles().size();

  if (m_gw2Path.isEmpty()) {
    m_statusLabel->setText("[!] GW2 path not set - run Setup Wizard");
    UIHelpers::applyWarningColorRole(m_statusLabel);
  } else if (count > 0) {
    m_statusLabel->setText(QString("[OK] GW2 is running (%1 instance%2)")
                               .arg(count)
                               .arg(count > 1 ? "s" : ""));
    UIHelpers::applySuccessColorRole(m_statusLabel);
  } else {
    m_statusLabel->setText("[OK] Ready to launch");
    UIHelpers::applyStatusRole(m_statusLabel);
  }
}

void LauncherWidget::refreshRunningStates() {
  // Validate all running profiles (clear stale PIDs)
  m_profileManager->validateRunningProfiles();

  // Refresh the profile list to update running badges
  updateProfileList();

  // Refresh the bottom status bar count
  updateStatus();

  qInfo() << "LauncherWidget: Refreshed running states";
}

void LauncherWidget::launchOrFocusProfile(const QString &profileId) {
  // Get profile from manager (returns persistent pointer)
  AccountProfile *profile = m_profileManager->profile(profileId);

  if (!profile) {
    qWarning() << "launchOrFocusProfile: Profile not found:" << profileId;
    return;
  }

  // Option C: If running, focus the window; otherwise launch
  if (m_profileManager->isProfileRunning(profileId)) {
    qInfo() << "launchOrFocusProfile: Profile" << profileId
            << "is running - focusing window";
    m_profileManager->bringProfileWindowToFocus(profileId);
  } else {
    qInfo() << "launchOrFocusProfile: Profile" << profileId
            << "not running - launching";

    // Pre-launch build check (foolproof - every launch path must check)
    QString effectivePath =
        LaunchManager::getEffectiveGw2Path(*profile, m_gw2Path);
    if (effectivePath.endsWith(".exe", Qt::CaseInsensitive)) {
      QFileInfo fi(effectivePath);
      effectivePath = fi.absolutePath();
    }

    QWidget *parentWindow = window();
    if (parentWindow) {
      bool okToProceed = true;
      QMetaObject::invokeMethod(
          parentWindow, "checkGW2BuildBeforeLaunch", Qt::DirectConnection,
          Q_RETURN_ARG(bool, okToProceed), Q_ARG(QString, effectivePath));
      if (!okToProceed) {
        qInfo() << "launchOrFocusProfile: Launch blocked - update needed for"
                << effectivePath;
        return;
      }
    }

    // Per-profile build update: ensure this profile's Local.dat
    // has been through the current GW2 build before launching with args
    if (!runPerProfileBuildUpdate(*profile)) {
      qInfo() << "launchOrFocusProfile: Profile build update cancelled";
      return;
    }

    m_launchManager->launchWithProfile(*profile);
  }
}

bool LauncherWidget::waitForProfilePid(const QString &profileId) {
  // If already running (from direct launch that returned PID immediately),
  // return true
  if (m_profileManager->isProfileRunning(profileId)) {
    qInfo() << "waitForProfilePid: Profile" << profileId
            << "already has PID confirmed";
    return true;
  }

  AccountProfile *profile = m_profileManager->profile(profileId);
  if (!profile) {
    qWarning() << "waitForProfilePid: Profile not found:" << profileId;
    return false;
  }

  // For standalone profiles, PID was set immediately via emit profileLaunched
  // If we get here and it's standalone, something went wrong - return false
  if (profile->accountProvider == AccountProvider::Standalone) {
    qWarning()
        << "waitForProfilePid: Standalone profile should already have PID";
    return false;
  }

  // Update and show the permanent cancel container (no layout shift!)
  QString iconPath =
      profile->icon.isEmpty() || !profile->icon.startsWith(":/icons/")
          ? ":/icons/profile-game.svg"
          : profile->icon;
  m_cancelIcon->setPixmap(QIcon(iconPath).pixmap(24, 24));
  m_cancelText->setText(
      QString("Waiting for <b>%1</b>...").arg(profile->nickname));
  m_cancelContainer->show();
  QCoreApplication::processEvents(); // Ensure UI updates

  // Use QEventLoop to wait for the profileRunningStateChanged signal
  // (event-based, NO TIMERS)
  QEventLoop loop;
  bool confirmed = false;
  bool cancelled = false;

  // Connect to signal - when this profile's PID is set, exit the loop
  QMetaObject::Connection conn1 =
      connect(m_profileManager, &ProfileManager::profileRunningStateChanged,
              &loop, [&](const QString &id, bool running) {
                if (id == profileId && running) {
                  confirmed = true;
                  loop.quit();
                }
              });

  // Connect to watcher cancelled signal (platform closed, another launch
  // cancelled it, etc.)
  QMetaObject::Connection conn2 =
      connect(GW2WindowWatcher::instance(), &GW2WindowWatcher::watchCancelled,
              &loop, [&](const QString &id) {
                if (id == profileId) {
                  cancelled = true;
                  loop.quit();
                }
              });

  // Connect cancel button
  QMetaObject::Connection conn3 =
      connect(m_cancelBtn, &QPushButton::clicked, &loop, [&]() {
        qInfo() << "waitForProfilePid: User cancelled wait for:" << profileId;
        cancelled = true;
        GW2WindowWatcher::instance()
            ->stopWatching(); // This will also emit watchCancelled
        loop.quit();
      });

  // Enter the event loop - this will block until signal or cancel
  loop.exec();

  // Cleanup
  disconnect(conn1);
  disconnect(conn2);
  disconnect(conn3);

  // Hide cancel container (stays in layout, no shift)
  m_cancelContainer->hide();

  if (confirmed) {
    qInfo() << "waitForProfilePid: PID confirmed for profile" << profileId;
  } else if (cancelled) {
    qWarning() << "waitForProfilePid: Cancelled for profile" << profileId;
    m_statusLabel->setText(
        QString("%1 launch cancelled").arg(profile->nickname));
  }

  return confirmed;
}

// =========================================================================
// TRIGGER-BASED WAIT: Block until GW2 window is detected for a profile.
// This is the gate that confirms GW2 has read Local.dat before the
// junction can safely switch to the next profile or be deactivated.
// Uses QEventLoop + profileWindowConfirmed signal — ZERO timers.
// =========================================================================
bool LauncherWidget::waitForWindowConfirm(const QString &profileId) {
  AccountProfile *profile = m_profileManager->profile(profileId);
  if (!profile) {
    qWarning() << "waitForWindowConfirm: Profile not found:" << profileId;
    return false;
  }

  m_statusLabel->setText(
      QString("Waiting for %1 window...").arg(profile->nickname));
  QCoreApplication::processEvents();

  QEventLoop loop;
  bool confirmed = false;

  // TRIGGER: profileWindowConfirmed fires from onGW2WindowDetected
  auto conn1 = connect(m_launchManager, &LaunchManager::profileWindowConfirmed,
                       &loop, [&](const QString &id) {
                         if (id == profileId) {
                           confirmed = true;
                           loop.quit();
                         }
                       });

  // SAFETY: gw2Exited fires if process dies before window (crash)
  auto conn2 = connect(
      m_launchManager, &LaunchManager::gw2Exited, &loop, [&](qint64, int) {
        // Check if THIS profile's process died
        if (!m_profileManager->isProfileRunning(profileId)) {
          qWarning() << "waitForWindowConfirm: Profile" << profile->nickname
                     << "exited before window detected";
          loop.quit();
        }
      });

  // SAFETY: watchCancelled fires if user cancels
  auto conn3 =
      connect(GW2WindowWatcher::instance(), &GW2WindowWatcher::watchCancelled,
              &loop, [&](const QString &id) {
                if (id == profileId) {
                  qWarning() << "waitForWindowConfirm: Watch cancelled for"
                             << profile->nickname;
                  loop.quit();
                }
              });

  loop.exec();

  disconnect(conn1);
  disconnect(conn2);
  disconnect(conn3);

  if (confirmed) {
    qInfo() << "waitForWindowConfirm: Window confirmed for"
            << profile->nickname;
  }

  return confirmed;
}

AccountProfile *LauncherWidget::selectedProfile() {
  auto *item = m_profileList->currentItem();
  if (!item)
    return nullptr;

  QString id = item->data(Qt::UserRole).toString();
  return m_profileManager->profile(id);
}

void LauncherWidget::onAddProfile() {
  AccountProfile newProfile;
  newProfile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

  // Auto-increment name to prevent duplicates
  QString baseName = "New Profile";
  QString candidateName = baseName;
  QSet<QString> existingNames;
  for (const auto &p : m_profileManager->profiles()) {
    existingNames.insert(p.nickname);
  }
  int counter = 2;
  while (existingNames.contains(candidateName)) {
    candidateName = baseName + " " + QString::number(counter++);
  }
  newProfile.nickname = candidateName;
  newProfile.arguments << "-maploadinfo";

  ProfileEditor editor(&newProfile, m_profileManager, m_serverManager,
                       m_dataService, m_markerController, this);
  if (editor.exec() == QDialog::Accepted) {
    AccountProfile p = editor.getProfile();
    // Ensure ID is preserved from the original
    p.id = newProfile.id;

    // Add this complete profile directly to the list
    m_profileManager->addCompleteProfile(p);
  }
}

void LauncherWidget::onEditProfile() {
  // Check if multiple profiles are selected (toggled ON)
  QList<QString> selectedIds = getSelectedProfileIds();
  if (selectedIds.size() > 1) {
    // Styled dialog for multiple selection
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("editInfoBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *lb = UIHelpers::createLabel(
        ct, QString("Select only one profile to edit.\n\nYou have %1 profiles "
                    "selected.")
                .arg(selectedIds.size()));
    lb->setAlignment(Qt::AlignCenter);
    cl->addWidget(lb);
    ly->addWidget(ct);
    auto *ok = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(ok);
    ok->setMinimumHeight(36);
    connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    ly->addWidget(ok);
    UIHelpers::centerDialog(d);
    d->exec();
    d->deleteLater();
    return;
  }

  // Use first selected, or fall back to list selection
  AccountProfile *profile = nullptr;
  if (!selectedIds.isEmpty()) {
    profile = m_profileManager->profile(selectedIds.first());
  } else {
    profile = selectedProfile();
  }

  if (!profile) {
    // Styled dialog for no selection
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("editInfoBg2");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *lb = UIHelpers::createLabel(ct, "Select a profile first.");
    lb->setAlignment(Qt::AlignCenter);
    cl->addWidget(lb);
    ly->addWidget(ct);
    auto *ok = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(ok);
    ok->setMinimumHeight(36);
    connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    ly->addWidget(ok);
    UIHelpers::centerDialog(d);
    d->exec();
    d->deleteLater();
    return;
  }

  ProfileEditor editor(profile, m_profileManager, m_serverManager,
                       m_dataService, m_markerController, this);
  if (editor.exec() == QDialog::Accepted) {
    AccountProfile updated = editor.getProfile();
    updated.id = profile->id; // Keep the same ID
    m_profileManager->updateProfile(updated);
    updateProfileList();
  }
}

void LauncherWidget::onDeleteProfile() {
  // Get selected profiles (toggled ON), or fall back to list selection
  QList<QString> selectedIds = getSelectedProfileIds();

  if (selectedIds.isEmpty()) {
    auto *item = m_profileList->currentItem();
    if (item) {
      selectedIds.append(item->data(Qt::UserRole).toString());
    }
  }

  if (selectedIds.isEmpty()) {
    // Styled "no selection" dialog
    auto *infoDialog = UIHelpers::createStyledDialog(this, 350);
    auto *outerLayout = new QVBoxLayout(infoDialog);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    auto *bgContainer = new QWidget();
    bgContainer->setObjectName("infoBg");
    UIHelpers::applyPopupBackgroundRole(bgContainer);
    outerLayout->addWidget(bgContainer);
    auto *layout = new QVBoxLayout(bgContainer);
    layout->setContentsMargins(20, 20, 20, 20);
    auto *container = UIHelpers::createMessageContainer(bgContainer);
    auto *containerLayout = qobject_cast<QVBoxLayout *>(container->layout());
    auto *label = UIHelpers::createLabel(container, "Select a profile first.");
    label->setAlignment(Qt::AlignCenter);
    containerLayout->addWidget(label);
    layout->addWidget(container);
    auto *okBtn = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(okBtn);
    okBtn->setMinimumHeight(36);
    connect(okBtn, &QPushButton::clicked, infoDialog, &QDialog::accept);
    layout->addWidget(okBtn);
    UIHelpers::centerDialog(infoDialog);
    infoDialog->exec();
    infoDialog->deleteLater();
    return;
  }

  // Build confirmation message
  QString message;
  if (selectedIds.size() == 1) {
    AccountProfile *profile = m_profileManager->profile(selectedIds.first());
    message = QString("Delete profile '%1'?")
                  .arg(profile ? profile->nickname : "Unknown");
  } else {
    message =
        QString("Delete %1 selected profiles?\n\n").arg(selectedIds.size());
    for (const QString &id : selectedIds) {
      AccountProfile *p = m_profileManager->profile(id);
      if (p)
        message += QString("• %1\n").arg(p->nickname);
    }
  }
  // Styled delete confirmation dialog
  auto *dialog = UIHelpers::createStyledDialog(this, 400);
  auto *outerLayout = new QVBoxLayout(dialog);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  auto *bgContainer = new QWidget();
  bgContainer->setObjectName("deleteBg");
  UIHelpers::applyPopupBackgroundRole(bgContainer);
  outerLayout->addWidget(bgContainer);

  auto *layout = new QVBoxLayout(bgContainer);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(15);

  auto *container = UIHelpers::createMessageContainer(bgContainer);
  auto *containerLayout = qobject_cast<QVBoxLayout *>(container->layout());

  auto *titleLabel = UIHelpers::createLabel(container, "Delete Profile", 16);
  UIHelpers::applyGoldTitleRole(titleLabel);
  titleLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(titleLabel);

  auto *msgLabel = UIHelpers::createLabel(container, message);
  msgLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(msgLabel);

  layout->addWidget(container);

  // Yes/No buttons
  auto *btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(12);

  auto *noBtn = new QPushButton("No");
  UIHelpers::applyCancelStyle(noBtn);
  noBtn->setMinimumHeight(36);
  connect(noBtn, &QPushButton::clicked, dialog, &QDialog::reject);
  btnLayout->addWidget(noBtn);

  auto *yesBtn = new QPushButton("Yes");
  UIHelpers::applyPrimaryStyle(yesBtn);
  yesBtn->setMinimumHeight(36);
  connect(yesBtn, &QPushButton::clicked, dialog, &QDialog::accept);
  btnLayout->addWidget(yesBtn);

  layout->addLayout(btnLayout);

  UIHelpers::centerDialog(dialog);

  if (dialog->exec() == QDialog::Accepted) {
    for (const QString &id : selectedIds) {
      m_profileManager->removeProfile(id);
    }
    updateProfileList();
  }
  dialog->deleteLater();
}

// REVIEW BEFORE BETA: Audit finding #13 — this method shares ~300 lines of
// near-identical code with onLaunchAll() below. Deferred dedup due to subtle
// behavioral differences. See docs/audit-report-phase1.md for details.
void LauncherWidget::onLaunchSelected() {
  if (m_gw2Path.isEmpty()) {
    // Styled warning dialog
    auto *d = UIHelpers::createStyledDialog(this, 400);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("launchWarnBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *tl = UIHelpers::createLabel(ct, "Launch Failed", 16);
    UIHelpers::applyGoldTitleRole(tl);
    tl->setAlignment(Qt::AlignCenter);
    cl->addWidget(tl);
    auto *lb = UIHelpers::createLabel(
        ct, "GW2 path not set. Please run the Setup Wizard.");
    lb->setAlignment(Qt::AlignCenter);
    cl->addWidget(lb);
    ly->addWidget(ct);
    auto *ok = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(ok);
    ok->setMinimumHeight(36);
    connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    ly->addWidget(ok);
    UIHelpers::centerDialog(d);
    d->exec();
    d->deleteLater();
    return;
  }

  // Get profiles with toggles ON first (need them to determine paths to
  // check)
  QList<QString> selectedIds = getSelectedProfileIds();

  // If no toggles selected, use the list selection as fallback
  if (selectedIds.isEmpty()) {
    AccountProfile *profile = selectedProfile();
    if (profile) {
      selectedIds.append(profile->id);
    }
  }

  if (selectedIds.isEmpty()) {
    // Styled no selection dialog
    auto *d = UIHelpers::createStyledDialog(this, 400);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("noSelBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *lb =
        UIHelpers::createLabel(ct, "Toggle ON the profiles you want to launch, "
                                   "or select one from the list.");
    lb->setAlignment(Qt::AlignCenter);
    cl->addWidget(lb);
    ly->addWidget(ct);
    auto *ok = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(ok);
    ok->setMinimumHeight(36);
    connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    ly->addWidget(ok);
    UIHelpers::centerDialog(d);
    d->exec();
    d->deleteLater();
    return;
  }

  // --- Multibox-OFF guard ---
  // Count how many non-running profiles are about to launch
  int profilesToLaunch = 0;
  for (const QString &id : selectedIds) {
    if (!m_profileManager->isProfileRunning(id))
      profilesToLaunch++;
  }
  if (!checkMultiboxGuard(profilesToLaunch))
    return;

  // --- Pre-flight credential refresh ---
  if (m_multiBoxToggle->isChecked() && profilesToLaunch > 0) {
    // Collect profiles being launched for staleness check
    QList<AccountProfile> launchingProfiles;
    for (const QString &id : selectedIds) {
      AccountProfile *p = m_profileManager->profile(id);
      if (p && !m_profileManager->isProfileRunning(id))
        launchingProfiles.append(*p);
    }
    if (!runPreFlightRefresh(launchingProfiles))
      return;
  }

  // Collect all unique GW2 paths from selected profiles
  QSet<QString> uniquePaths;
  for (const QString &id : selectedIds) {
    AccountProfile *profile = m_profileManager->profile(id);
    if (!profile)
      continue;

    // Get effective path for this profile using platform detection
    // For Steam/Epic profiles with no custom path, this detects the
    // correct installation from platform manifests (not global fallback)
    QString effectivePath =
        LaunchManager::getEffectiveGw2Path(*profile, m_gw2Path);

    // Normalize: if path ends with .exe, extract parent directory
    if (effectivePath.endsWith(".exe", Qt::CaseInsensitive)) {
      QFileInfo fi(effectivePath);
      effectivePath = fi.absolutePath();
    }
    uniquePaths.insert(effectivePath);
  }

  // Pre-launch GW2 build check for EACH unique path
  QWidget *parentWindow = window();
  if (parentWindow) {
    for (const QString &path : uniquePaths) {
      bool okToProceed = true;
      QMetaObject::invokeMethod(
          parentWindow, "checkGW2BuildBeforeLaunch", Qt::DirectConnection,
          Q_RETURN_ARG(bool, okToProceed), Q_ARG(QString, path));
      if (!okToProceed) {
        qInfo() << "LauncherWidget: Launch cancelled - update needed for path:"
                << path;
        return;
      }
    }
  }

  // --- Pre-launch batch build update ---
  // All profiles needing build updates are refreshed BEFORE any launches.
  // Phase 1 (-image) deduped by path, Phase 2 (Local.dat refresh) per profile.
  {
    QList<AccountProfile> batchProfiles;
    for (const QString &id : selectedIds) {
      AccountProfile *p = m_profileManager->profile(id);
      if (p && !m_profileManager->isProfileRunning(id))
        batchProfiles.append(*p);
    }
    if (!runBatchBuildUpdate(batchProfiles))
      return;
  }

  // Sort: Standalone first, then Steam/Epic.
  // Standalone profiles must create Local.dat symlinks BEFORE Steam/Epic
  // GW2 processes lock the file. See features/local-dat-management.md
  std::sort(selectedIds.begin(), selectedIds.end(),
            [this](const QString &a, const QString &b) {
              auto *pa = m_profileManager->profile(a);
              auto *pb = m_profileManager->profile(b);
              bool aStandalone =
                  pa && pa->accountProvider == AccountProvider::Standalone;
              bool bStandalone =
                  pb && pb->accountProvider == AccountProvider::Standalone;
              return aStandalone > bStandalone;
            });

  // Note: No pre-backup needed — junction approach activates each profile
  // folder individually, no shared Local.dat to backup.

  // Launch each selected profile
  int launched = 0;
  int alreadyRunning = 0;
  for (const QString &id : selectedIds) {
    AccountProfile *originalProfile = m_profileManager->profile(id);
    if (!originalProfile)
      continue;

    // Skip if profile is already running
    if (m_profileManager->isProfileRunning(id)) {
      qInfo() << "Profile" << originalProfile->nickname
              << "is already running - skipping";

      // Try to focus the window
      bool focused = m_profileManager->bringProfileWindowToFocus(id);

      m_statusLabel->setText(QString("%1 is already running%2")
                                 .arg(originalProfile->nickname)
                                 .arg(focused ? " - window focused" : ""));
      UIHelpers::applySuccessColorRole(m_statusLabel);

      alreadyRunning++;
      continue;
    }

    // IMPORTANT: Work on a COPY to avoid accumulating args on repeated
    // launches
    AccountProfile profile = *originalProfile;

    // Add extra args to the COPY
    QString extraArgs = m_argsEdit->text().trimmed();
    if (!extraArgs.isEmpty()) {
      QStringList args = extraArgs.split(' ', Qt::SkipEmptyParts);
      for (const QString &arg : args) {
        if (!profile.arguments.contains(arg)) {
          profile.arguments.append(arg);
        }
      }
    }

    // Add network settings based on profile's NetworkMode
    // First, strip any conflicting network args from the Arguments tab
    // This prevents conflicts when using UseGlobal or Custom modes
    if (profile.networkMode == NetworkMode::UseGlobal ||
        profile.networkMode == NetworkMode::Custom) {
      QMutableStringListIterator it(profile.arguments);
      while (it.hasNext()) {
        QString arg = it.next();
        if (arg.startsWith("-authsrv") || arg.startsWith("-portal") ||
            arg.startsWith("-assetsrv") || arg.startsWith("-clientport")) {
          it.remove();
        }
      }
    }

    switch (profile.networkMode) {
    case NetworkMode::UseGlobal:
      // Use global server settings from ServerManager
      if (m_serverManager) {
        QString authArg = m_serverManager->authServerArg();
        QString assetArg = m_serverManager->assetServerArg();
        QString portArg = m_serverManager->clientPortArg();

        if (!authArg.isEmpty()) {
          profile.arguments.append(authArg);
        }
        if (!assetArg.isEmpty()) {
          profile.arguments.append(assetArg);
        }
        if (!portArg.isEmpty()) {
          profile.arguments.append(portArg);
        }
      }
      break;

    case NetworkMode::Custom:
      // Use profile's custom server
      if (!profile.customNetworkServer.isEmpty()) {
        QString authArg =
            QString("-authsrv %1").arg(profile.customNetworkServer);
        profile.arguments.append(authArg);
      }
      break;

    case NetworkMode::UseDefault:
    default:
      // No custom args - use ArenaNet defaults (like unmodified launcher)
      break;
    }

    // For multi-boxing after first launch: kill previous instance's mutex
    // so GW2 allows the next instance. Mutex kill happens AFTER window
    // trigger confirms the previous GW2 has fully initialized.
    if (launched > 0 && m_multiBoxToggle->isChecked()) {
      m_statusLabel->setText(
          QString("Closing mutex to enable multibox (%1/%2)...")
              .arg(launched)
              .arg(selectedIds.size()));
      QCoreApplication::processEvents();

      // Single instant call — mutex exists because window trigger
      // already confirmed GW2 is past initialization
      bool mutexKilled = m_launchManager->closeMutexForMultiBox();
      if (mutexKilled) {
        qInfo()
            << "Mutex killed (post-window-trigger) — ready for next profile";
      } else {
        qWarning() << "Mutex not found after window trigger — launching anyway";
      }
    }

    // Build update already handled by runBatchBuildUpdate above
    QProcess *proc = m_launchManager->launchWithProfile(profile);

    // For Steam/Epic, proc is nullptr but launch is still successful
    // (URL-based launch)
    bool launchSuccess = proc != nullptr ||
                         profile.accountProvider == AccountProvider::Steam ||
                         profile.accountProvider == AccountProvider::Epic;

    if (launchSuccess) {
      launched++;
      m_statusLabel->setText(
          QString("Launched %1/%2...").arg(launched).arg(selectedIds.size()));
      QCoreApplication::processEvents();

      // Save lastLoginTime from the copy back to the original profile
      originalProfile->lastLoginTime = profile.lastLoginTime;
      m_profileManager->updateProfile(*originalProfile);

      // TRIGGER-BASED: Wait for GW2 window to confirm Local.dat was read.
      // This gates junction switching: the junction can only change AFTER
      // this profile's GW2 has file handles to its own Local.dat.
      waitForWindowConfirm(id);
    } else {
      qWarning() << "Failed to launch profile:" << profile.nickname;
    }
  }

  // === Post-launch: Deactivate junction, restore original AppData ===
  // Safe because the last profile's window was confirmed above via
  // waitForWindowConfirm() — GW2 has file handles to its own Local.dat.
  if (launched > 0) {
    m_launchManager->deactivateJunction();
  }

  // Refresh profile list to show updated last login times
  updateProfileList();

  if (launched == 0 && alreadyRunning == 0) {
    // Styled warning dialog
    auto *d = UIHelpers::createStyledDialog(this, 400);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("launchFailBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *tl = UIHelpers::createLabel(ct, "Launch Failed", 16);
    UIHelpers::applyGoldTitleRole(tl);
    tl->setAlignment(Qt::AlignCenter);
    cl->addWidget(tl);
    auto *lb = UIHelpers::createLabel(
        ct, "Failed to launch any profiles. Check the GW2 path.");
    lb->setAlignment(Qt::AlignCenter);
    cl->addWidget(lb);
    ly->addWidget(ct);
    auto *ok = new QPushButton("OK");
    UIHelpers::applyPrimaryStyle(ok);
    ok->setMinimumHeight(36);
    connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    ly->addWidget(ok);
    UIHelpers::centerDialog(d);
    d->exec();
    d->deleteLater();
  } else if (launched == 0 && alreadyRunning > 0) {
    m_statusLabel->setText(QString("All %1 selected profile(s) already running")
                               .arg(alreadyRunning));
    UIHelpers::applySuccessColorRole(m_statusLabel);
  } else {
    m_statusLabel->setText(
        QString("[OK] Launched %1 profile(s)%2")
            .arg(launched)
            .arg(alreadyRunning > 0
                     ? QString(", %1 already running").arg(alreadyRunning)
                     : ""));
  }
}

// REVIEW BEFORE BETA: Audit finding #13 — this method shares ~300 lines of
// near-identical code with onLaunchSelected() above. Deferred dedup due to
// subtle behavioral differences. See docs/audit-report-phase1.md for details.
void LauncherWidget::onLaunchAll() {
  if (m_gw2Path.isEmpty()) {
    // Styled warning dialog
    auto *d = UIHelpers::createStyledDialog(this, 380);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("launchFailBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("GW2 path not set. Please run the Setup Wizard.");
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

  const QList<AccountProfile> &profiles = m_profileManager->profiles();
  if (profiles.isEmpty()) {
    // Styled info dialog
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("noProfilesBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("No profiles to launch. Add profiles first.");
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

  // --- Multibox-OFF guard ---
  int profilesToLaunch = 0;
  for (const AccountProfile &p : profiles) {
    if (!m_profileManager->isProfileRunning(p.id))
      profilesToLaunch++;
  }
  if (!checkMultiboxGuard(profilesToLaunch))
    return;

  // --- Pre-flight credential refresh ---
  if (m_multiBoxToggle->isChecked() && profilesToLaunch > 0) {
    QList<AccountProfile> launchingProfiles;
    for (const AccountProfile &p : profiles) {
      if (!m_profileManager->isProfileRunning(p.id))
        launchingProfiles.append(p);
    }
    if (!runPreFlightRefresh(launchingProfiles))
      return;
  }

  // Collect all unique GW2 paths from ALL profiles
  QSet<QString> uniquePaths;
  for (const AccountProfile &profile : profiles) {
    QString effectivePath =
        LaunchManager::getEffectiveGw2Path(profile, m_gw2Path);

    // Normalize: if path ends with .exe, extract parent directory
    if (effectivePath.endsWith(".exe", Qt::CaseInsensitive)) {
      QFileInfo fi(effectivePath);
      effectivePath = fi.absolutePath();
    }
    uniquePaths.insert(effectivePath);
  }

  // Pre-launch GW2 build check for EACH unique path
  QWidget *parentWindow = window();
  if (parentWindow) {
    for (const QString &path : uniquePaths) {
      bool okToProceed = true;
      QMetaObject::invokeMethod(
          parentWindow, "checkGW2BuildBeforeLaunch", Qt::DirectConnection,
          Q_RETURN_ARG(bool, okToProceed), Q_ARG(QString, path));
      if (!okToProceed) {
        qInfo() << "LauncherWidget: Launch All cancelled - update needed for "
                   "path:"
                << path;
        return;
      }
    }
  }

  // --- Pre-launch batch build update ---
  // All profiles needing build updates are refreshed BEFORE any launches.
  {
    QList<AccountProfile> batchProfiles;
    for (const AccountProfile &p : profiles) {
      if (!m_profileManager->isProfileRunning(p.id))
        batchProfiles.append(p);
    }
    if (!runBatchBuildUpdate(batchProfiles))
      return;
  }

  // Sort: Standalone first, then Steam/Epic (same reason as onLaunchSelected)
  QList<AccountProfile> sortedProfiles = profiles;
  std::sort(sortedProfiles.begin(), sortedProfiles.end(),
            [](const AccountProfile &a, const AccountProfile &b) {
              bool aStandalone =
                  a.accountProvider == AccountProvider::Standalone;
              bool bStandalone =
                  b.accountProvider == AccountProvider::Standalone;
              return aStandalone > bStandalone;
            });

  // Note: No pre-backup needed — junction approach activates each profile
  // folder individually, no shared Local.dat to backup.

  // Launch each profile with proper multiboxing support (same as
  // onLaunchSelected)
  int launched = 0;
  int alreadyRunning = 0;
  for (int i = 0; i < sortedProfiles.size(); i++) {
    const AccountProfile &originalProfile = sortedProfiles[i];

    // Skip if profile is already running
    if (m_profileManager->isProfileRunning(originalProfile.id)) {
      qInfo() << "Profile" << originalProfile.nickname
              << "is already running - skipping (Launch All)";

      // Try to focus the window
      m_profileManager->bringProfileWindowToFocus(originalProfile.id);

      alreadyRunning++;
      continue;
    }

    AccountProfile profile =
        originalProfile; // Make a copy (launchWithProfile may modify)

    // For multi-boxing after first launch: kill previous instance's mutex
    // Mutex kill happens AFTER window trigger confirms previous GW2 initialized
    if (launched > 0 && m_multiBoxToggle->isChecked()) {
      m_statusLabel->setText(QString("Closing mutex for multibox (%1/%2)...")
                                 .arg(launched)
                                 .arg(sortedProfiles.size()));
      QApplication::processEvents();

      // Single instant call - mutex exists because window trigger
      // already confirmed GW2 is past initialization
      bool mutexKilled = m_launchManager->closeMutexForMultiBox();
      if (mutexKilled) {
        qInfo() << "Mutex killed (post-window-trigger) - ready for next";
      } else {
        qWarning() << "Mutex not found after window trigger - launching anyway";
      }
    }

    // Build update already handled by runBatchBuildUpdate above

    // Launch with full profile support
    QProcess *proc = m_launchManager->launchWithProfile(profile);
    if (proc || profile.accountProvider == AccountProvider::Steam ||
        profile.accountProvider == AccountProvider::Epic) {
      launched++;
      m_statusLabel->setText(QString("Launched %1/%2...")
                                 .arg(launched)
                                 .arg(sortedProfiles.size()));
      QApplication::processEvents();

      // Save updated profile (lastLoginTime was set in launchWithProfile)
      m_profileManager->updateProfile(profile);

      // TRIGGER-BASED: Wait for GW2 window to confirm Local.dat was read.
      // This gates junction switching: the junction can only change AFTER
      // this profile's GW2 has file handles to its own Local.dat.
      waitForWindowConfirm(originalProfile.id);
    } else {
      qWarning() << "Failed to launch profile:" << profile.nickname;
    }
  }

  // === Post-launch: Deactivate junction, restore original AppData ===
  // Safe because the last profile's window was confirmed above via
  // waitForWindowConfirm() - GW2 has file handles to its own Local.dat.
  if (launched > 0) {
    m_launchManager->deactivateJunction();
  }

  // Refresh profile list to show updated last login times
  updateProfileList();

  if (launched == 0 && alreadyRunning == 0) {
    // Styled warning dialog
    auto *d = UIHelpers::createStyledDialog(this, 380);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("launchFailBg2");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("Failed to launch any profiles. Check the GW2 path.");
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
  } else if (launched == 0 && alreadyRunning > 0) {
    m_statusLabel->setText(
        QString("All %1 profile(s) already running").arg(alreadyRunning));
    UIHelpers::applySuccessColorRole(m_statusLabel);
  } else {
    m_statusLabel->setText(
        QString("[OK] Launched %1 profile(s)%2")
            .arg(launched)
            .arg(alreadyRunning > 0
                     ? QString(", %1 already running").arg(alreadyRunning)
                     : ""));
  }
}

void LauncherWidget::onCheckForUpdates() {
  // Get selected profiles, or all if none selected
  QList<QString> selectedIds = getSelectedProfileIds();
  if (selectedIds.isEmpty()) {
    // If none selected, patch all profiles
    auto profiles = m_profileManager->profiles();
    for (const auto &p : profiles) {
      selectedIds.append(p.id);
    }
  }

  if (selectedIds.isEmpty()) {
    // Styled info dialog
    auto *d = UIHelpers::createStyledDialog(this, 300);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("noCheckBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("No profiles to check.");
    UIHelpers::applyPopupLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
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

  // Styled confirmation dialog
  auto *d = UIHelpers::createStyledDialog(this, 450);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  bg->setObjectName("checkBg");
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);
  ly->setSpacing(15);
  auto *tl = new QLabel("Check for Updates");
  UIHelpers::applyGoldTitleRole(tl);
  tl->setAlignment(Qt::AlignCenter);
  ly->addWidget(tl);
  auto *lb = new QLabel(
      QString("This will launch %1 profile(s) one at a time to check for "
              "updates.\n\nEach will launch without -shareArchive or multibox "
              "arguments.\nClose each game after it finishes patching to "
              "continue.\n\nContinue?")
          .arg(selectedIds.size()));
  UIHelpers::applyPopupLabelRole(lb);
  lb->setAlignment(Qt::AlignCenter);
  lb->setWordWrap(true);
  ly->addWidget(lb);
  auto *btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(12);
  auto *noBtn = new QPushButton("No");
  noBtn->setMinimumHeight(36);
  UIHelpers::applyCancelStyle(noBtn);
  connect(noBtn, &QPushButton::clicked, d, &QDialog::reject);
  btnLayout->addWidget(noBtn);
  auto *yesBtn = new QPushButton("Yes");
  yesBtn->setMinimumHeight(36);
  UIHelpers::applyPrimaryStyle(yesBtn);
  connect(yesBtn, &QPushButton::clicked, d, &QDialog::accept);
  btnLayout->addWidget(yesBtn);
  ly->addLayout(btnLayout);
  UIHelpers::centerDialog(d);

  if (d->exec() != QDialog::Accepted) {
    d->deleteLater();
    return;
  }
  d->deleteLater();

  // Collect unique GW2 paths — avoids launching the same install multiple times
  QSet<QString> uniquePaths;
  QStringList pathOrder; // Maintain order for status messages

  for (const QString &id : selectedIds) {
    auto *profile = m_profileManager->profile(id);
    if (!profile)
      continue;

    QString effectivePath =
        LaunchManager::getEffectiveGw2Path(*profile, m_gw2Path);
    if (!uniquePaths.contains(effectivePath)) {
      uniquePaths.insert(effectivePath);
      pathOrder.append(effectivePath);
      qInfo() << "Patch queue: added path" << effectivePath
              << "from profile:" << profile->nickname;
    } else {
      qInfo() << "Patch queue: skipping duplicate path" << effectivePath
              << "from profile:" << profile->nickname;
    }
  }

  int patched = 0;
  for (const QString &effectivePath : pathOrder) {
    m_statusLabel->setText(QString("Patching path %1/%2...")
                               .arg(patched + 1)
                               .arg(pathOrder.size()));
    QCoreApplication::processEvents();

    QString exePath = effectivePath + "/Gw2-64.exe";
    qInfo() << "Patching path" << effectivePath << "- launching bare GW2 exe";

#ifdef Q_OS_WIN
    QString cmdLine =
        QString("\"%1\"").arg(exePath); // No arguments for patching
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::wstring cmdLineW = cmdLine.toStdWString();
    std::wstring workDirW = effectivePath.toStdWString();

    if (CreateProcessW(nullptr, const_cast<wchar_t *>(cmdLineW.c_str()),
                       nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                       workDirW.c_str(), &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      patched++;

      // Styled patching dialog
      auto *d = UIHelpers::createStyledDialog(this, 400);
      auto *ol = new QVBoxLayout(d);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      bg->setObjectName("patchBg");
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      auto *lb = new QLabel(
          QString("Launched GW2 for patching.\nPath: %1\n\nWait for patching "
                  "to complete, then close the game and click OK.")
              .arg(effectivePath));
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
    } else {
      qWarning() << "Failed to launch for patching:" << GetLastError();
    }
#endif
  }

  m_statusLabel->setText(
      QString("[OK] Checked %1 path(s) for updates").arg(patched));
}

void LauncherWidget::onProfileDoubleClicked(QListWidgetItem *item) {
  Q_UNUSED(item);
  onLaunchSelected();
}

void LauncherWidget::onProfileContextMenu(const QPoint &pos) {
  auto *item = m_profileList->itemAt(pos);
  if (!item)
    return;

  QMenu menu;
  menu.addAction("Launch", this, &LauncherWidget::onLaunchSelected);
  menu.addSeparator();
  menu.addAction("Edit", this, &LauncherWidget::onEditProfile);
  menu.addAction("Clone", this, &LauncherWidget::onCloneProfile);
  menu.addSeparator();
  menu.addAction("Export Profile...", this, &LauncherWidget::onExportProfile);
  menu.addAction("Import Profile...", this, &LauncherWidget::onImportProfile);
  menu.addSeparator();
  menu.addAction("Move Up", this, &LauncherWidget::onMoveProfileUp);
  menu.addAction("Move Down", this, &LauncherWidget::onMoveProfileDown);
  menu.addSeparator();
  menu.addAction("Delete", this, &LauncherWidget::onDeleteProfile);

  menu.exec(m_profileList->mapToGlobal(pos));
}

void LauncherWidget::onCloneProfile() {
  // Check if multiple profiles are selected (toggled ON)
  QList<QString> selectedIds = getSelectedProfileIds();
  if (selectedIds.size() > 1) {
    // Styled info dialog
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("cloneBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb =
        new QLabel("Select only one profile to clone.\n\nYou have " +
                   QString::number(selectedIds.size()) + " profiles selected.");
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

  // Use first selected, or fall back to list selection
  AccountProfile *profile = nullptr;
  if (!selectedIds.isEmpty()) {
    profile = m_profileManager->profile(selectedIds.first());
  } else {
    profile = selectedProfile();
  }

  if (!profile) {
    // Styled info dialog
    auto *d = UIHelpers::createStyledDialog(this, 300);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("cloneBg2");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("Select a profile first.");
    UIHelpers::applyPopupLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
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

  // Create a copy of profile settings (but NOT the id - that comes from
  // addProfile)
  AccountProfile clone = *profile;

  // Smart clone naming - avoid "Copy (Copy)" situations
  // Pattern: "Name - Copy", "Name - Copy 2", "Name - Copy 3", etc.
  QString baseName = profile->nickname;

  // Remove existing " - Copy" or " - Copy N" suffix if present
  static QRegularExpression copyPattern(" - Copy(?: (\\d+))?$");
  baseName.remove(copyPattern);

  // Find the next available copy number
  QString newName = baseName + " - Copy";
  int copyNum = 1;
  while (m_profileManager->profileByNickname(newName) != nullptr) {
    copyNum++;
    newName = QString("%1 - Copy %2").arg(baseName).arg(copyNum);
  }

  clone.nickname = newName;

  // Clear login data for security - cloned profile should have its own
  // credentials
  clone.localDatPath = ""; // Don't copy saved login

  // Add new profile to manager - this generates the proper ID
  QString newId = m_profileManager->addProfile(clone.nickname);
  auto *added = m_profileManager->profile(newId);
  if (added) {
    // Copy all settings from clone, but KEEP the ID that addProfile generated
    QString correctId = added->id;
    *added = clone;
    added->id = correctId; // Restore the correct ID
    m_profileManager->updateProfile(*added);
  }

  updateProfileList();

  // Styled clone success dialog
  auto *dialog = UIHelpers::createStyledDialog(this, 400);
  auto *outerLayout = new QVBoxLayout(dialog);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  auto *bgContainer = new QWidget();
  bgContainer->setObjectName("cloneBg");
  UIHelpers::applyPopupBackgroundRole(bgContainer);
  outerLayout->addWidget(bgContainer);

  auto *layout = new QVBoxLayout(bgContainer);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(15);

  auto *container = UIHelpers::createMessageContainer(bgContainer);
  auto *containerLayout = qobject_cast<QVBoxLayout *>(container->layout());

  auto *titleLabel = UIHelpers::createLabel(container, "Clone Profile", 16);
  UIHelpers::applyGoldTitleRole(titleLabel);
  titleLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(titleLabel);

  auto *msgLabel = UIHelpers::createLabel(
      container, QString("Profile '%1' cloned as '%2'")
                     .arg(profile->nickname, clone.nickname));
  msgLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(msgLabel);

  layout->addWidget(container);

  auto *okBtn = new QPushButton("OK");
  UIHelpers::applyPrimaryStyle(okBtn);
  okBtn->setMinimumHeight(36);
  connect(okBtn, &QPushButton::clicked, dialog, &QDialog::accept);
  layout->addWidget(okBtn);

  UIHelpers::centerDialog(dialog);
  dialog->exec();
  dialog->deleteLater();
}

void LauncherWidget::onMoveProfileUp() {
  // Find which profile is toggled ON
  QString toggledProfileId;
  int toggledCount = 0;
  for (auto it = m_profileToggles.begin(); it != m_profileToggles.end(); ++it) {
    if (it.value()->isChecked()) {
      toggledProfileId = it.key();
      toggledCount++;
    }
  }

  if (toggledCount == 0) {
    // Styled info dialog
    auto *d = UIHelpers::createStyledDialog(this, 300);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("moveUpBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("Please select a profile to move.");
    UIHelpers::applyPopupLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
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
  if (toggledCount > 1) {
    // Styled info dialog
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("moveUpBg2");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("Only one profile can be moved at a time.\nPlease "
                          "select only one profile.");
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

  // Find the index of the toggled profile
  int row = -1;
  const auto &profiles = m_profileManager->profiles();
  for (int i = 0; i < profiles.size(); i++) {
    if (profiles[i].id == toggledProfileId) {
      row = i;
      break;
    }
  }

  if (row <= 0)
    return; // Already at top or not found

  // Save toggle states before rebuilding list
  QMap<QString, bool> toggleStates;
  for (auto it = m_profileToggles.begin(); it != m_profileToggles.end(); ++it) {
    toggleStates[it.key()] = it.value()->isChecked();
  }

  m_profileManager->moveProfile(row, row - 1);
  updateProfileList();
  m_profileList->setCurrentRow(row - 1);

  // Restore toggle states
  for (auto it = toggleStates.begin(); it != toggleStates.end(); ++it) {
    if (m_profileToggles.contains(it.key())) {
      m_profileToggles[it.key()]->setChecked(it.value());
    }
  }
}

void LauncherWidget::onMoveProfileDown() {
  // Find which profile is toggled ON
  QString toggledProfileId;
  int toggledCount = 0;
  for (auto it = m_profileToggles.begin(); it != m_profileToggles.end(); ++it) {
    if (it.value()->isChecked()) {
      toggledProfileId = it.key();
      toggledCount++;
    }
  }

  if (toggledCount == 0) {
    // Styled info dialog
    auto *d = UIHelpers::createStyledDialog(this, 300);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("moveDownBg");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("Please select a profile to move.");
    UIHelpers::applyPopupLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
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
  if (toggledCount > 1) {
    // Styled info dialog
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    bg->setObjectName("moveDownBg2");
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("Only one profile can be moved at a time.\nPlease "
                          "select only one profile.");
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

  // Find the index of the toggled profile
  int row = -1;
  const auto &profiles = m_profileManager->profiles();
  for (int i = 0; i < profiles.size(); i++) {
    if (profiles[i].id == toggledProfileId) {
      row = i;
      break;
    }
  }

  if (row < 0 || row >= profiles.size() - 1)
    return; // At bottom or not found

  // Save toggle states before rebuilding list
  QMap<QString, bool> toggleStates;
  for (auto it = m_profileToggles.begin(); it != m_profileToggles.end(); ++it) {
    toggleStates[it.key()] = it.value()->isChecked();
  }

  m_profileManager->moveProfile(row, row + 1);
  updateProfileList();
  m_profileList->setCurrentRow(row + 1);

  // Restore toggle states
  for (auto it = toggleStates.begin(); it != toggleStates.end(); ++it) {
    if (m_profileToggles.contains(it.key())) {
      m_profileToggles[it.key()]->setChecked(it.value());
    }
  }
}

void LauncherWidget::onSettings() {
  QString path = QFileDialog::getExistingDirectory(
      this, "Select GW2 Installation Folder", m_gw2Path);
  if (!path.isEmpty()) {
    // Check if valid
    QDir dir(path);
    if (dir.exists("Gw2-64.exe") || dir.exists("Gw2.exe")) {
      setGw2Path(path);
      // Styled success dialog
      auto *d = UIHelpers::createStyledDialog(this, 300);
      auto *ol = new QVBoxLayout(d);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      bg->setObjectName("settingsBg");
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      auto *lb = new QLabel("GW2 path updated successfully!");
      UIHelpers::applySuccessColorRole(lb);
      lb->setAlignment(Qt::AlignCenter);
      ly->addWidget(lb);
      auto *ok = new QPushButton("OK");
      ok->setMinimumHeight(36);
      UIHelpers::applyPrimaryStyle(ok);
      connect(ok, &QPushButton::clicked, d, &QDialog::accept);
      ly->addWidget(ok);
      UIHelpers::centerDialog(d);
      d->exec();
      d->deleteLater();
    } else {
      // Styled warning dialog
      auto *d = UIHelpers::createStyledDialog(this, 380);
      auto *ol = new QVBoxLayout(d);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      bg->setObjectName("invalidPathBg");
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      auto *lb =
          new QLabel("The selected folder doesn't contain GW2 executable.");
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
  }
}

void LauncherWidget::onExportProfile() {
  // Get selected profile
  QList<QString> selectedIds = getSelectedProfileIds();
  if (selectedIds.size() > 1) {
    // Multi-select: export all selected
    QString folder = QFileDialog::getExistingDirectory(
        this, "Select Export Folder", QDir::homePath());
    if (folder.isEmpty())
      return;

    int exported = 0;
    for (const QString &id : selectedIds) {
      auto *profile = m_profileManager->profile(id);
      if (!profile)
        continue;
      QString filePath =
          QDir(folder).filePath(profile->nickname + ".gw2profile");
      if (m_profileManager->exportProfile(id, filePath)) {
        exported++;
      }
    }

    // Styled success dialog
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel(
        QString("Exported %1 profile(s) to:\n%2").arg(exported).arg(folder));
    UIHelpers::applySuccessLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
    lb->setWordWrap(true);
    ly->addWidget(lb);
    auto *note = new QLabel("Note: Login data (Local.dat) is not exported.");
    UIHelpers::applyHintRole(note);
    note->setAlignment(Qt::AlignCenter);
    ly->addWidget(note);
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

  // Single profile export
  AccountProfile *profile = nullptr;
  if (!selectedIds.isEmpty()) {
    profile = m_profileManager->profile(selectedIds.first());
  } else {
    profile = selectedProfile();
  }

  if (!profile) {
    auto *d = UIHelpers::createStyledDialog(this, 300);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("Select a profile first.");
    UIHelpers::applyPopupLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
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

  QString defaultName = profile->nickname + ".gw2profile";
  QString filePath = QFileDialog::getSaveFileName(
      this, "Export Profile", QDir::homePath() + "/" + defaultName,
      "GW2 Profile (*.gw2profile)");

  if (filePath.isEmpty())
    return;

  if (m_profileManager->exportProfile(profile->id, filePath)) {
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel(
        QString("Profile '%1' exported successfully!").arg(profile->nickname));
    UIHelpers::applySuccessLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
    lb->setWordWrap(true);
    ly->addWidget(lb);
    auto *note = new QLabel("Note: Login data (Local.dat) is not exported.");
    UIHelpers::applyHintRole(note);
    note->setAlignment(Qt::AlignCenter);
    ly->addWidget(note);
    auto *ok = new QPushButton("OK");
    ok->setMinimumHeight(36);
    UIHelpers::applyPrimaryStyle(ok);
    connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    ly->addWidget(ok);
    UIHelpers::centerDialog(d);
    d->exec();
    d->deleteLater();
  } else {
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("Failed to export profile.");
    UIHelpers::applyErrorTitleRole(lb);
    lb->setAlignment(Qt::AlignCenter);
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
}

void LauncherWidget::onImportProfile() {
  QStringList filePaths = QFileDialog::getOpenFileNames(
      this, "Import Profiles", QDir::homePath(),
      "GW2 Profile (*.gw2profile);;All Files (*)");

  if (filePaths.isEmpty())
    return;

  int imported = 0;
  int failed = 0;
  QSet<QString> importedPaths; // Collect unique GW2 paths

  for (const QString &filePath : filePaths) {
    QString resolvedPath;
    if (m_dataService->importProfileWithChecks(filePath, resolvedPath)) {
      imported++;
      if (!resolvedPath.isEmpty()) {
        importedPaths.insert(resolvedPath);
      }
    } else {
      failed++;
    }
  }

  updateProfileList();

  // Show result dialog
  auto *d = UIHelpers::createStyledDialog(this, 350);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);

  QString message;
  if (imported > 0 && failed == 0) {
    message = QString("Successfully imported %1 profile(s)!").arg(imported);
  } else if (imported > 0 && failed > 0) {
    message = QString("Imported %1 profile(s).\n%2 failed to import.")
                  .arg(imported)
                  .arg(failed);
  } else {
    message =
        "Failed to import profile(s).\n\nPlease check the file(s) are valid.";
  }

  auto *lb = new QLabel(message);
  if (imported > 0 && failed == 0) {
    UIHelpers::applySuccessLabelRole(lb);
  } else if (imported > 0 && failed > 0) {
    UIHelpers::applyWarningColorRole(lb);
  } else {
    UIHelpers::applyErrorTitleRole(lb);
  }
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

  // After result dialog — mandatory update check for each unique GW2 path
  if (imported > 0) {
    QWidget *parentWindow = window();

    for (const QString &path : importedPaths) {
      QString validPath = path;

      if (!QDir(validPath).exists()) {
        // Path doesn't exist (exported from another machine)
        UIHelpers::showInfoDialog(
            this, "The imported profile points to a GW2 installation "
                  "that doesn't exist on this machine.\n\n"
                  "Please select your GW2 installation folder.");

        validPath = QFileDialog::getExistingDirectory(
            this, "Select GW2 Installation Folder", QDir::homePath());

        if (validPath.isEmpty()) {
          // User cancelled — profile imported but can't launch
          // until they set a valid path in Profile Editor
          continue;
        }
      }

      // Run mandatory update check via MainWindow
      if (parentWindow) {
        bool okToProceed = true;
        QMetaObject::invokeMethod(
            parentWindow, "checkGW2BuildBeforeLaunch", Qt::DirectConnection,
            Q_RETURN_ARG(bool, okToProceed), Q_ARG(QString, validPath));
        // We don't block on the result here — just trigger the check.
        // If an update is needed, the dialog handles it.
      }
    }
  }
}

bool LauncherWidget::checkMultiboxGuard(int profileCount) {
  if (profileCount <= 1 || m_multiBoxToggle->isChecked())
    return true; // Single profile or multibox ON — OK to proceed

  // Multiple profiles with multibox OFF — show styled dialog
  auto *d = UIHelpers::createStyledDialog(this, 450);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 16, 20, 20);

  auto *titleBar = UIHelpers::createTitleBar(
      bg, "Multi-Box Required", "alert-yellow", [d]() { d->reject(); });
  ly->addWidget(titleBar);

  auto *ct = UIHelpers::createMessageContainer(bg);
  auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
  auto *msg = UIHelpers::createLabel(
      ct, QString("You have %1 profiles selected but Multi-Box Mode is OFF.\n\n"
                  "Launching multiple GW2 instances requires multiboxing to "
                  "close the game's mutex between launches.")
              .arg(profileCount));
  msg->setAlignment(Qt::AlignCenter);
  msg->setWordWrap(true);
  cl->addWidget(msg);
  ly->addWidget(ct);

  ly->addSpacing(8);

  auto *btnLay = new QHBoxLayout();
  auto *cancelBtn = new QPushButton("Cancel");
  cancelBtn->setMinimumHeight(36);
  UIHelpers::applyCancelStyle(cancelBtn);
  connect(cancelBtn, &QPushButton::clicked, d, &QDialog::reject);
  btnLay->addWidget(cancelBtn);

  auto *enableBtn = new QPushButton("Enable Multi-Box && Launch");
  UIHelpers::setThemedIcon(enableBtn, "play-circle");
  enableBtn->setMinimumHeight(36);
  UIHelpers::applyConfirmStyle(enableBtn);
  connect(enableBtn, &QPushButton::clicked, d, &QDialog::accept);
  btnLay->addWidget(enableBtn);
  ly->addLayout(btnLay);

  UIHelpers::centerDialog(d);
  int result = d->exec();
  d->deleteLater();

  if (result == QDialog::Accepted) {
    // Set multibox state immediately — must happen BEFORE launch loop
    // (toggleWithAnimation is async and would fire the signal too late)
    m_multiBoxToggle->setChecked(true);
    m_launchManager->setMultiBoxEnabled(true);
    return true;
  }
  return false;
}

bool LauncherWidget::runPreFlightRefresh(
    const QList<AccountProfile> &profiles) {
  if (!m_credRefreshMgr)
    return true;

  auto stale = m_credRefreshMgr->getStaleProfiles(profiles);
  if (stale.isEmpty())
    return true; // All fresh — proceed

  qInfo() << "Pre-flight: detected" << stale.size()
          << "stale profiles, starting refresh";

  // Build check: ensure GW2 is up-to-date before refreshing credentials.
  // Without this, GW2 launches into "pending download" error and wastes time.
  QWidget *parentWindow = window();
  if (parentWindow) {
    QSet<QString> checkedPaths;
    for (const auto &p : stale) {
      QString effectivePath =
          LaunchManager::getEffectiveGw2Path(p, m_gw2Path);
      if (effectivePath.endsWith(".exe", Qt::CaseInsensitive)) {
        QFileInfo fi(effectivePath);
        effectivePath = fi.absolutePath();
      }
      if (checkedPaths.contains(effectivePath))
        continue;
      checkedPaths.insert(effectivePath);

      bool okToProceed = true;
      QMetaObject::invokeMethod(
          parentWindow, "checkGW2BuildBeforeLaunch", Qt::DirectConnection,
          Q_RETURN_ARG(bool, okToProceed), Q_ARG(QString, effectivePath));
      if (!okToProceed) {
        qInfo() << "Pre-flight refresh blocked - update needed for"
                << effectivePath;
        return false;
      }
    }
  }

  // Create progress dialog
  auto *d = UIHelpers::createStyledDialog(this, 420);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 16, 20, 20);

  auto *titleBar = UIHelpers::createTitleBar(
      bg, "Refreshing Credentials", ":/icons/refresh.svg", [this, d]() {
        m_credRefreshMgr->cancel();
        d->reject();
      });
  ly->addWidget(titleBar);

  auto *ct = UIHelpers::createMessageContainer(bg);
  auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
  auto *statusLbl =
      UIHelpers::createLabel(ct, "Launching GW2 to refresh credentials...");
  statusLbl->setAlignment(Qt::AlignCenter);
  statusLbl->setWordWrap(true);
  cl->addWidget(statusLbl);

  auto *hintLbl = UIHelpers::createLabel(
      ct,
      "Log in to the character select screen.\n"
      "AIO will detect the updated credentials and continue automatically.");
  hintLbl->setAlignment(Qt::AlignCenter);
  hintLbl->setWordWrap(true);
  UIHelpers::applyHintRole(hintLbl);
  cl->addWidget(hintLbl);

  auto *progressLbl = UIHelpers::createLabel(ct, "");
  progressLbl->setAlignment(Qt::AlignCenter);
  UIHelpers::applyHintRole(progressLbl);
  cl->addWidget(progressLbl);
  ly->addWidget(ct);

  auto *cancelBtn = new QPushButton("Cancel");
  cancelBtn->setMinimumHeight(36);
  UIHelpers::applyCancelStyle(cancelBtn);
  connect(cancelBtn, &QPushButton::clicked, d, [this, d]() {
    m_credRefreshMgr->cancel();
    d->reject();
  });
  ly->addWidget(cancelBtn);

  // Connect progress signals
  connect(
      m_credRefreshMgr, &CredentialRefreshManager::refreshProgress, d,
      [statusLbl, progressLbl](int current, int total, const QString &name) {
        statusLbl->setText(
            QString("Refreshing credentials for %1...").arg(name));
        progressLbl->setText(
            QString("Profile %1 of %2").arg(current).arg(total));
      });

  connect(m_credRefreshMgr, &CredentialRefreshManager::refreshFailed, d,
          [progressLbl](const QString &name, const QString &reason) {
            progressLbl->setText(
                QString("Warning: %1 — %2 (continuing...)").arg(name, reason));
          });

  connect(m_credRefreshMgr, &CredentialRefreshManager::refreshComplete, d,
          &QDialog::accept);

  UIHelpers::centerDialog(d);

  // Start refresh
  m_credRefreshMgr->refreshProfiles(stale);

  // Block until refresh completes or is cancelled
  int result = d->exec();
  d->deleteLater();

  return (result == QDialog::Accepted);
}

bool LauncherWidget::runPerProfileBuildUpdate(AccountProfile &profile) {
  // Only standalone profiles need per-profile updating.
  // Steam/Epic use platform auth — their dat files aren't profile-managed.
  if (profile.accountProvider != AccountProvider::Standalone)
    return true;

  auto *um = m_dataService->updateManager();
  if (!um)
    return true;

  int remoteBuild = um->remoteBuildId();
  if (remoteBuild <= 0)
    return true; // Can't check without remote build

  // Already verified at this build — no update needed
  if (profile.lastVerifiedBuild == remoteBuild)
    return true;

  qInfo() << "Per-profile build update needed for" << profile.nickname
          << "— profile build:" << profile.lastVerifiedBuild
          << "remote:" << remoteBuild;

  // Resolve the GW2 path for this profile
  QString effectivePath =
      LaunchManager::getEffectiveGw2Path(profile, m_gw2Path);
  if (effectivePath.endsWith(".exe", Qt::CaseInsensitive)) {
    QFileInfo fi(effectivePath);
    effectivePath = fi.absolutePath();
  }

  QString exePath = effectivePath + "/Gw2-64.exe";
  if (!QFileInfo::exists(exePath)) {
    qWarning() << "Per-profile update: Gw2-64.exe not found at" << exePath;
    return true; // Can't update, allow launch anyway
  }

  // === Use -image flag to update game data (industry standard) ===
  // -image tells GW2 to download/update its dat files and EXIT automatically.
  // No login, no user interaction needed. No junction needed either — -image
  // only touches Gw2.dat in the install directory, not profile-specific data.
  // This is the same approach used by GW2Launcher (Healix) and LaunchBuddy.

  // Create styled update dialog
  auto *d = UIHelpers::createStyledDialog(this, 440);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 16, 20, 20);

  auto *titleBar = UIHelpers::createTitleBar(
      bg, "Updating Game Data", ":/icons/refresh.svg", [d]() {
        d->reject();
      });
  ly->addWidget(titleBar);

  auto *ct = UIHelpers::createMessageContainer(bg);
  auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
  auto *statusLbl = UIHelpers::createLabel(
      ct, QString("Updating game data for <b>%1</b>...<br>"
                  "This is automatic and will complete on its own.")
              .arg(profile.nickname));
  statusLbl->setAlignment(Qt::AlignCenter);
  statusLbl->setWordWrap(true);
  cl->addWidget(statusLbl);

  auto *hintLbl = UIHelpers::createLabel(
      ct,
      "GW2 is downloading the latest game data.\n"
      "The dialog will close automatically when complete.");
  hintLbl->setAlignment(Qt::AlignCenter);
  hintLbl->setWordWrap(true);
  UIHelpers::applyHintRole(hintLbl);
  cl->addWidget(hintLbl);
  ly->addWidget(ct);

  auto *cancelBtn = new QPushButton("Cancel");
  cancelBtn->setMinimumHeight(36);
  UIHelpers::applyCancelStyle(cancelBtn);
  connect(cancelBtn, &QPushButton::clicked, d, &QDialog::reject);
  ly->addWidget(cancelBtn);

  UIHelpers::centerDialog(d);

  // Launch GW2 with -image flag — updates dat files and exits automatically
  QProcess *imageProc = new QProcess(d);
  imageProc->setProgram(exePath);
  imageProc->setArguments({"-image"});
  imageProc->setWorkingDirectory(effectivePath);
  imageProc->start();

  if (!imageProc->waitForStarted(5000)) {
    qWarning() << "Per-profile update: Failed to start GW2 -image for"
               << profile.nickname;
    d->deleteLater();
    return true; // Failed to launch, allow normal launch anyway
  }

  qInfo() << "Per-profile update: Launched GW2 -image, PID:"
          << imageProc->processId() << "for" << profile.nickname;

  // Auto-close dialog when GW2 -image exits (it exits automatically)
  connect(imageProc,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), d,
          &QDialog::accept);

  int result = d->exec();

  // If user cancelled, terminate the -image process
  if (result != QDialog::Accepted &&
      imageProc->state() != QProcess::NotRunning) {
    imageProc->terminate();
    imageProc->waitForFinished(3000);
    if (imageProc->state() != QProcess::NotRunning) {
      imageProc->kill();
    }
  }

  d->deleteLater();

  if (result != QDialog::Accepted) {
    qInfo() << "Per-profile update: User cancelled for" << profile.nickname;
    return false; // User cancelled
  }

  // -image completed successfully — Gw2.dat is up to date.
  // Now we need to update the profile's Local.dat too.
  // -image only updates the shared game archive, NOT the per-profile Local.dat.
  // With -shareArchive, GW2 opens Local.dat read-only — if it's stale (wrong
  // build), GW2 can't reconcile and shows "Download failed (error 5)".
  // Fix: launch with junction active + -autologin ONLY (no -shareArchive)
  // so GW2 can update Local.dat in write mode.

  qInfo() << "Per-profile update: Phase 2 — refreshing Local.dat for"
          << profile.nickname;

  auto *localDatMgr = m_dataService->localDatManager();
  if (localDatMgr && !profile.id.isEmpty()) {
    localDatMgr->activateProfile(profile.id);
  }

  // Create new dialog for Phase 2
  auto *d2 = UIHelpers::createStyledDialog(this, 440);
  auto *ol2 = new QVBoxLayout(d2);
  ol2->setContentsMargins(0, 0, 0, 0);
  auto *bg2 = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bg2);
  ol2->addWidget(bg2);
  auto *ly2 = new QVBoxLayout(bg2);
  ly2->setContentsMargins(20, 16, 20, 20);

  auto *titleBar2 = UIHelpers::createTitleBar(
      bg2, "Refreshing Profile Data", ":/icons/refresh.svg", [d2]() {
        d2->reject();
      });
  ly2->addWidget(titleBar2);

  auto *ct2 = UIHelpers::createMessageContainer(bg2);
  auto *cl2 = qobject_cast<QVBoxLayout *>(ct2->layout());
  auto *statusLbl2 = UIHelpers::createLabel(
      ct2,
      QString("Refreshing profile data for <b>%1</b>...<br>"
              "GW2 is logging in to update this profile's Local.dat.")
          .arg(profile.nickname));
  statusLbl2->setAlignment(Qt::AlignCenter);
  statusLbl2->setWordWrap(true);
  cl2->addWidget(statusLbl2);

  auto *hintLbl2 = UIHelpers::createLabel(
      ct2,
      "Close GW2 after it reaches the character select screen.\n"
      "AIO will then relaunch with your full profile settings.");
  hintLbl2->setAlignment(Qt::AlignCenter);
  hintLbl2->setWordWrap(true);
  UIHelpers::applyHintRole(hintLbl2);
  cl2->addWidget(hintLbl2);
  ly2->addWidget(ct2);

  auto *cancelBtn2 = new QPushButton("Cancel");
  cancelBtn2->setMinimumHeight(36);
  UIHelpers::applyCancelStyle(cancelBtn2);
  connect(cancelBtn2, &QPushButton::clicked, d2, &QDialog::reject);
  ly2->addWidget(cancelBtn2);

  UIHelpers::centerDialog(d2);

  // Launch with -autologin ONLY — no -shareArchive so Local.dat can be written
  QProcess *refreshProc = new QProcess(d2);
  refreshProc->setProgram(exePath);
  refreshProc->setArguments({"-autologin"});
  refreshProc->setWorkingDirectory(effectivePath);
  refreshProc->start();

  if (!refreshProc->waitForStarted(5000)) {
    qWarning() << "Per-profile update: Phase 2 failed to start for"
               << profile.nickname;
    d2->deleteLater();
    // Deactivate junction
    if (localDatMgr && localDatMgr->isJunctionActive()) {
      localDatMgr->deactivateProfile();
    }
    return true; // Allow normal launch anyway
  }

  qInfo() << "Per-profile update: Phase 2 launched -autologin (no "
             "-shareArchive), PID:"
          << refreshProc->processId() << "for" << profile.nickname;

  // Wait for GW2 to exit — user closes it after reaching character select.
  // No timers — updates can take longer than any fixed timeout.
  connect(refreshProc,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), d2,
          &QDialog::accept);

  int result2 = d2->exec();

  // Ensure process is stopped
  if (refreshProc->state() != QProcess::NotRunning) {
    refreshProc->terminate();
    refreshProc->waitForFinished(3000);
    if (refreshProc->state() != QProcess::NotRunning) {
      refreshProc->kill();
    }
  }

  d2->deleteLater();

  // Deactivate junction after Local.dat refresh
  if (localDatMgr && localDatMgr->isJunctionActive()) {
    localDatMgr->deactivateProfile();
  }

  bool phase2Success = (result2 == QDialog::Accepted);

  if (phase2Success) {
    profile.lastVerifiedBuild = remoteBuild;
    m_profileManager->updateProfile(profile);
    qInfo() << "Per-profile update complete:" << profile.nickname
            << "verified at build" << remoteBuild;
  } else {
    qInfo() << "Per-profile update: Phase 2 cancelled — NOT marking"
            << profile.nickname << "as verified (will retry next launch)";
  }

  return phase2Success;
}

bool LauncherWidget::runBatchBuildUpdate(QList<AccountProfile> &profiles) {
  auto *um = m_dataService->updateManager();
  if (!um)
    return true;

  int remoteBuild = um->remoteBuildId();
  if (remoteBuild <= 0)
    return true;

  // Collect profiles needing build updates (standalone only)
  QList<int> needUpdate; // indices into profiles list
  for (int i = 0; i < profiles.size(); i++) {
    auto &p = profiles[i];
    if (p.accountProvider != AccountProvider::Standalone)
      continue;
    if (p.lastVerifiedBuild == remoteBuild)
      continue;
    needUpdate.append(i);
  }

  if (needUpdate.isEmpty())
    return true; // All profiles are up to date

  qInfo() << "Batch build update:" << needUpdate.size()
          << "profiles need updating to build" << remoteBuild;

  // === Phase 1: -image per unique install path (path-level dedup) ===
  QSet<QString> updatedPaths;
  for (int idx : needUpdate) {
    const auto &p = profiles[idx];
    QString effectivePath =
        LaunchManager::getEffectiveGw2Path(p, m_gw2Path);
    if (effectivePath.endsWith(".exe", Qt::CaseInsensitive)) {
      QFileInfo fi(effectivePath);
      effectivePath = fi.absolutePath();
    }

    if (updatedPaths.contains(effectivePath))
      continue;
    updatedPaths.insert(effectivePath);

    QString exePath = effectivePath + "/Gw2-64.exe";
    if (!QFileInfo::exists(exePath)) {
      qWarning() << "Batch update: Gw2-64.exe not found at" << exePath;
      continue;
    }

    qInfo() << "Batch update: Phase 1 — running -image for path:"
            << effectivePath;

    // Launch -image silently — GW2 updates dat and exits automatically
    QProcess imageProc;
    imageProc.setProgram(exePath);
    imageProc.setArguments({"-image"});
    imageProc.setWorkingDirectory(effectivePath);
    imageProc.start();

    if (!imageProc.waitForStarted(5000)) {
      qWarning() << "Batch update: Failed to start -image for" << exePath;
      continue;
    }

    qInfo() << "Batch update: -image started, PID:" << imageProc.processId();

    // Wait for -image to finish (exits automatically)
    imageProc.waitForFinished(-1); // No timeout — -image handles itself

    qInfo() << "Batch update: -image completed for path:" << effectivePath;
  }

  // === Phase 2: Per-profile Local.dat refresh ===
  auto *localDatMgr = m_dataService->localDatManager();

  for (int step = 0; step < needUpdate.size(); step++) {
    int idx = needUpdate[step];
    auto &profile = profiles[idx];

    QString effectivePath =
        LaunchManager::getEffectiveGw2Path(profile, m_gw2Path);
    if (effectivePath.endsWith(".exe", Qt::CaseInsensitive)) {
      QFileInfo fi(effectivePath);
      effectivePath = fi.absolutePath();
    }

    QString exePath = effectivePath + "/Gw2-64.exe";
    if (!QFileInfo::exists(exePath))
      continue;

    qInfo() << "Batch update: Phase 2 — refreshing Local.dat for"
            << profile.nickname << "(" << (step + 1) << "/"
            << needUpdate.size() << ")";

    // Activate junction for this profile
    if (localDatMgr && !profile.id.isEmpty()) {
      localDatMgr->activateProfile(profile.id);
    }

    // Create dialog for this profile
    auto *d = UIHelpers::createStyledDialog(this, 440);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 16, 20, 20);

    auto *titleBar = UIHelpers::createTitleBar(
        bg, "Refreshing Profile Data", ":/icons/refresh.svg", [d]() {
          d->reject();
        });
    ly->addWidget(titleBar);

    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *statusLbl = UIHelpers::createLabel(
        ct,
        QString("Refreshing profile data for <b>%1</b>...<br>"
                "Profile %2 of %3")
            .arg(profile.nickname)
            .arg(step + 1)
            .arg(needUpdate.size()));
    statusLbl->setAlignment(Qt::AlignCenter);
    statusLbl->setWordWrap(true);
    cl->addWidget(statusLbl);

    auto *hintLbl = UIHelpers::createLabel(
        ct,
        "Close GW2 after it reaches the character select screen.\n"
        "AIO will then continue with the next profile.");
    hintLbl->setAlignment(Qt::AlignCenter);
    hintLbl->setWordWrap(true);
    UIHelpers::applyHintRole(hintLbl);
    cl->addWidget(hintLbl);
    ly->addWidget(ct);

    auto *cancelBtn = new QPushButton("Cancel");
    cancelBtn->setMinimumHeight(36);
    UIHelpers::applyCancelStyle(cancelBtn);
    connect(cancelBtn, &QPushButton::clicked, d, &QDialog::reject);
    ly->addWidget(cancelBtn);

    UIHelpers::centerDialog(d);

    // Launch with -autologin ONLY — no -shareArchive
    QProcess *refreshProc = new QProcess(d);
    refreshProc->setProgram(exePath);
    refreshProc->setArguments({"-autologin"});
    refreshProc->setWorkingDirectory(effectivePath);
    refreshProc->start();

    if (!refreshProc->waitForStarted(5000)) {
      qWarning() << "Batch update: Phase 2 failed to start for"
                 << profile.nickname;
      d->deleteLater();
      if (localDatMgr && localDatMgr->isJunctionActive()) {
        localDatMgr->deactivateProfile();
      }
      continue; // Skip this profile, try next
    }

    qInfo() << "Batch update: Phase 2 launched -autologin, PID:"
            << refreshProc->processId() << "for" << profile.nickname;

    // Wait for GW2 to exit — user closes after reaching character select
    connect(refreshProc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), d,
            &QDialog::accept);

    int result = d->exec();

    // If user cancelled, terminate process and abort remaining
    if (result != QDialog::Accepted) {
      if (refreshProc->state() != QProcess::NotRunning) {
        refreshProc->terminate();
        refreshProc->waitForFinished(3000);
        if (refreshProc->state() != QProcess::NotRunning) {
          refreshProc->kill();
        }
      }
      d->deleteLater();
      if (localDatMgr && localDatMgr->isJunctionActive()) {
        localDatMgr->deactivateProfile();
      }
      qInfo() << "Batch update: User cancelled at profile"
              << profile.nickname;
      return false; // Cancel all remaining launches
    }

    d->deleteLater();

    // Deactivate junction
    if (localDatMgr && localDatMgr->isJunctionActive()) {
      localDatMgr->deactivateProfile();
    }

    // Mark profile as verified
    profile.lastVerifiedBuild = remoteBuild;
    m_profileManager->updateProfile(profile);
    qInfo() << "Batch update: Profile" << profile.nickname
            << "verified at build" << remoteBuild;
  }

  qInfo() << "Batch build update complete:" << needUpdate.size()
          << "profiles updated";
  return true;
}

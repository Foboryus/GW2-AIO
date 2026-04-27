#include "ProfileEditor.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QStandardPaths>

#include "UIHelpers.h"
#include "core/DataService.h"

ProfileEditor::ProfileEditor(AccountProfile *profile,
                             ProfileManager *profileManager,
                             ServerManager *serverManager,
                             DataService *dataService,
                             MarkerController *markerController,
                             QWidget *parent)
    : QDialog(parent), m_profileManager(profileManager),
      m_dataService(dataService), m_serverManager(serverManager),
      m_markerController(markerController) {
  if (profile) {
    m_profile = *profile;
  }

  // Frameless window with rounded corners
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground); // Required for rounded corners
  setAttribute(Qt::WA_AlwaysShowToolTips); // Show tooltips even when unfocused
  setMinimumSize(960, 650);

  setupUI();
  loadProfile();

  // Connect modified signals AFTER loadProfile to avoid false dirty state
  connect(m_generalTab, &GeneralTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_argumentsTab, &ArgumentsTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_windowTab, &WindowTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_loginTab, &LoginTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_accountTab, &AccountTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_networkTab, &NetworkTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_graphicsTab, &GraphicsTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_addonsTab, &AddonsTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_hotkeysTab, &HotkeysTabWidget::modified, this,
          &ProfileEditor::markDirty);
  connect(m_radialTab, &RadialTabWidget::modified, this,
          &ProfileEditor::markDirty);
}

void ProfileEditor::setupUI() {
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  // Background container with gray border - use objectName to prevent cascade
  auto *bgContainer = new QWidget();
  UIHelpers::applyWindowBackgroundRole(bgContainer);
  auto *mainLayout = new QVBoxLayout(bgContainer);
  mainLayout->setContentsMargins(16, 12, 16, 16); // More padding

  outerLayout->addWidget(bgContainer);

  // === Custom Title Bar ===
  QString titleText = m_profile.nickname.isEmpty()
                          ? "New Profile"
                          : "Edit: " + m_profile.nickname;
  // Use actual profile icon instead of fixed icon
  // Extract base name from icon path for themedIcon (strip :/icons/ and .svg)
  QString iconName = "profile-game"; // default
  if (!m_profile.icon.isEmpty()) {
    iconName = QFileInfo(m_profile.icon).baseName();
  }
  auto *titleBar = UIHelpers::createTitleBar(
      bgContainer, titleText, iconName, [this]() { reject(); },
      &m_saveIndicator);
  mainLayout->addWidget(titleBar);

  m_stack = new QStackedWidget(bgContainer);

  // ===== Create tab widgets (order = visual order in tab bar) =====

  // Row 1: General, Login, Network, Arguments, Window
  m_generalTab = new GeneralTabWidget(m_profile, this);
  m_loginTab = new LoginTabWidget(m_profile, m_dataService, this);
  m_accountTab = new AccountTabWidget(m_profile, m_dataService, this);
  m_networkTab = new NetworkTabWidget(m_profile, m_serverManager, this);
  m_argumentsTab = new ArgumentsTabWidget(m_profile, m_standardArgs, this);
  m_windowTab = new WindowTabWidget(m_profile, this);

  // Row 2: Graphics, Addons, Hotkeys, Markers
  m_graphicsTab = new GraphicsTabWidget(m_profile, m_dataService, this);
  m_addonsTab = new AddonsTabWidget(m_profile, this);
  m_hotkeysTab = new HotkeysTabWidget(m_profile, m_dataService, this);

  // Radial tab
  m_radialTab = new RadialTabWidget(m_profile, m_dataService, this);

  // Add to stack in order
  m_stack->addWidget(m_generalTab);   // 0
  m_stack->addWidget(m_loginTab);     // 1
  m_stack->addWidget(m_accountTab);   // 2
  m_stack->addWidget(m_networkTab);   // 3
  m_stack->addWidget(m_argumentsTab); // 4
  m_stack->addWidget(m_windowTab);    // 5
  m_stack->addWidget(m_graphicsTab);  // 6
  m_stack->addWidget(m_addonsTab);    // 7
  m_stack->addWidget(m_hotkeysTab);   // 8
  m_stack->addWidget(m_radialTab);    // 9

  // Markers tab (conditional)
  if (m_markerController && m_dataService) {
    m_markersTab = new MarkersTabWidget(
        m_profile, m_dataService->markerSettings(), m_markerController, this);
    m_stack->addWidget(m_markersTab); // 10
  }

  // ===== 2-Row Tab Bar =====
  struct TabDef {
    QString icon;
    QString label;
  };
  QList<TabDef> row1 = {{"home", "General"},
                        {"lock", "Login"},
                        {"user", "Account"},
                        {"globe", "Network"},
                        {"terminal", "Arguments"},
                        {"layout", "Window"}};
  QList<TabDef> row2 = {
      {"monitor", "Graphics"}, {"tool", "Addons"}, {"keyboard", "Hotkeys"},
      {"target", "Radial"}};
  if (m_markersTab) {
    row2.append(TabDef{"layers", "Markers"});
  }

  auto createTabRow = [this](const QList<TabDef> &defs, int startIndex) {
    auto *rowLayout = new QHBoxLayout();
    rowLayout->setSpacing(0);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < defs.size(); ++i) {
      auto *btn =
          new QPushButton(UIHelpers::themedIcon(defs[i].icon), defs[i].label);
      btn->setCheckable(true);
      btn->setMinimumHeight(32);
      btn->setCursor(Qt::PointingHandCursor);
      btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      int tabIndex = startIndex + i;
      connect(btn, &QPushButton::clicked, this,
              [this, tabIndex]() { switchToTab(tabIndex); });
      rowLayout->addWidget(btn);
      m_tabButtons.append(btn);
    }
    return rowLayout;
  };

  mainLayout->addLayout(createTabRow(row1, 0));
  mainLayout->addLayout(createTabRow(row2, 6));

  // Lazy-load heavy tabs on first click
  // (onTabChanged handles this via m_stack->widget(index))

  mainLayout->addWidget(m_stack);

  // Apply initial tab selection
  switchToTab(0);

  // Dialog buttons with padding and centered
  auto *buttonContainer = new QWidget();
  auto *buttonLayout = new QHBoxLayout(buttonContainer);
  buttonLayout->setContentsMargins(20, 12, 20, 8);

  buttonLayout->addStretch();

  auto *cancelBtn = new QPushButton("Cancel");
  cancelBtn->setMinimumWidth(100);
  UIHelpers::applyCancelStyle(cancelBtn);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(cancelBtn);

  buttonLayout->addSpacing(12);

  m_applyBtn = new QPushButton("Apply");
  m_applyBtn->setMinimumWidth(100);
  m_applyBtn->setToolTip("Save changes without closing");
  UIHelpers::applyApplyCleanStyle(m_applyBtn);
  connect(m_applyBtn, &QPushButton::clicked, this, &ProfileEditor::onApply);
  buttonLayout->addWidget(m_applyBtn);

  buttonLayout->addSpacing(12);

  auto *saveBtn = new QPushButton("Save && Close");
  saveBtn->setMinimumWidth(120);
  UIHelpers::applyConfirmStyle(saveBtn);
  connect(saveBtn, &QPushButton::clicked, this, &ProfileEditor::onAccept);
  buttonLayout->addWidget(saveBtn);

  buttonLayout->addStretch();

  mainLayout->addWidget(buttonContainer);

  // Styles come from global QSS template (ThemeManager) — DO NOT use
  // setStyleSheet here, it blocks the app-level theme cascade for all
  // descendant widgets (QTabBar, QLineEdit, QGroupBox, etc.).
}

void ProfileEditor::loadProfile() {
  // General - delegate to tab widget
  m_generalTab->load();

  // Arguments - delegate to tab widget
  m_argumentsTab->load();

  // Window - delegate to tab widget
  m_windowTab->load();

  // Login - delegate to tab widget
  m_loginTab->load();

  // Account - delegate to tab widget
  m_accountTab->load();

  // GFX settings — lazy loaded on first tab click (see onTabChanged)

  // DLLs - delegate to tab widget
  m_addonsTab->load();

  // Network settings - delegate to tab widget
  m_networkTab->load();

  // Hotkeys - delegate to tab widget
  m_hotkeysTab->load();

  // Markers - delegate to tab widget (auto-saves independently)
  if (m_markersTab) {
    m_markersTab->load();
  }

  // Radial - delegate to tab widget
  m_radialTab->load();
}

void ProfileEditor::saveProfile() {
  // General - delegate to tab widget
  m_generalTab->save();

  // Arguments - delegate to tab widget
  m_argumentsTab->save();

  // === Steam/Epic: Strip incompatible settings ===
  if (m_profile.accountProvider == AccountProvider::Steam ||
      m_profile.accountProvider == AccountProvider::Epic) {

    // Disable auto-login (platform handles auth)
    m_profile.autoLogin = false;

    // Reset network to default (can't use custom servers)
    m_profile.networkMode = NetworkMode::UseDefault;
    m_profile.customNetworkServer.clear();

    // Strip incompatible arguments
    QStringList incompatibleArgs = {"-autologin",   "-provider",   "-authsrv",
                                    "-portal",      "-clientport", "-mumble",
                                    "-shareArchive"};
    QMutableStringListIterator it(m_profile.arguments);
    while (it.hasNext()) {
      QString arg = it.next();
      for (const QString &incompatible : incompatibleArgs) {
        if (arg.startsWith(incompatible, Qt::CaseInsensitive)) {
          it.remove();
          break;
        }
      }
    }
  }

  // Window - delegate to tab widget
  m_windowTab->save();

  // Login - delegate to tab widget
  m_loginTab->save();

  // Account - delegate to tab widget (no-op, read-only)
  m_accountTab->save();

  // GFX - delegate to tab widget
  m_graphicsTab->save();

  // DLLs - delegate to tab widget
  m_addonsTab->save();

  // Network settings - delegate to tab widget
  m_networkTab->save();

  // Hotkeys - delegate to tab widget
  m_hotkeysTab->save();

  // Radial - delegate to tab widget
  m_radialTab->save();
}

void ProfileEditor::onAccept() {
  if (m_generalTab->isNicknameEmpty()) {
    // Styled warning dialog
    auto *d = UIHelpers::createStyledDialog(this, 350);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *lb = UIHelpers::createLabel(ct, "Please enter a profile name.");
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
    m_generalTab->focusNickname();
    return;
  }

  saveProfile();

  // CRITICAL: Update ProfileManager's in-memory copy and persist to disk
  if (m_profileManager) {
    m_profileManager->updateProfile(m_profile);
  }

  accept();
}

void ProfileEditor::onApply() {
  if (m_generalTab->isNicknameEmpty()) {
    UIHelpers::showInfoDialog(this, "Please enter a profile name.");
    m_generalTab->focusNickname();
    return;
  }

  saveProfile();

  // CRITICAL: Update ProfileManager's in-memory copy so next load gets current
  // data
  if (m_profileManager) {
    m_profileManager->updateProfile(m_profile);
  }

  markClean();
}

void ProfileEditor::markDirty() {
  if (m_dirty)
    return;
  m_dirty = true;

  if (m_saveIndicator) {
    m_saveIndicator->setPixmap(UIHelpers::themedIcon("edit").pixmap(18, 18));
    m_saveIndicator->setToolTip("You have unsaved changes");
  }
  if (m_applyBtn) {
    UIHelpers::applyApplyDirtyStyle(m_applyBtn);
  }
}

void ProfileEditor::markClean() {
  m_dirty = false;

  if (m_saveIndicator) {
    m_saveIndicator->setPixmap(
        UIHelpers::themedIcon("check-circle").pixmap(18, 18));
    m_saveIndicator->setToolTip("All changes saved");
  }
  if (m_applyBtn) {
    UIHelpers::applyApplyCleanStyle(m_applyBtn);
  }
}

void ProfileEditor::onTabChanged(int index) {
  if (!m_stack) {
    return;
  }

  QWidget *currentTab = m_stack->widget(index);

  // Lazy-load Graphics tab on first visit
  if (currentTab == m_graphicsTab && !m_graphicsLoaded) {
    m_graphicsLoaded = true;
    m_graphicsTab->load();
  }
}

// ---------------------------------------------------------------------------
// Tab switching + styling
// ---------------------------------------------------------------------------

void ProfileEditor::switchToTab(int index) {
  m_stack->setCurrentIndex(index);
  onTabChanged(index);

  for (int i = 0; i < m_tabButtons.size(); ++i) {
    m_tabButtons[i]->setChecked(i == index);
    applyTabStyle(m_tabButtons[i], i == index);
  }
}

// REVIEW BEFORE BETA: inline styles — refactor to UIHelpers role-based styling
void ProfileEditor::applyTabStyle(QPushButton *btn, bool active) {
  if (active) {
    btn->setStyleSheet("QPushButton {"
                       "  background: transparent;"
                       "  border: none;"
                       "  border-bottom: 2px solid #C09C57;"
                       "  color: #C09C57;"
                       "  font-weight: bold;"
                       "  padding: 4px 8px;"
                       "}"
                       "QPushButton:hover {"
                       "  background: rgba(192, 156, 87, 0.1);"
                       "}");
  } else {
    btn->setStyleSheet("QPushButton {"
                       "  background: transparent;"
                       "  border: none;"
                       "  border-bottom: 2px solid transparent;"
                       "  color: #888888;"
                       "  font-weight: normal;"
                       "  padding: 4px 8px;"
                       "}"
                       "QPushButton:hover {"
                       "  color: #BBBBBB;"
                       "  border-bottom: 2px solid #555555;"
                       "}");
  }
}

void ProfileEditor::reject() {
  if (m_dirty) {
    auto *d = UIHelpers::createStyledDialog(this, 380);
    auto *ol = new QVBoxLayout(d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);

    auto *title = new QLabel("Unsaved Changes");
    UIHelpers::applyGoldTitleRole(title);
    title->setAlignment(Qt::AlignCenter);
    ly->addWidget(title);

    ly->addSpacing(8);

    auto *ct = UIHelpers::createMessageContainer(bg);
    auto *cl = qobject_cast<QVBoxLayout *>(ct->layout());
    auto *lb = UIHelpers::createLabel(
        ct, "You have unsaved changes.\nDiscard and close?");
    lb->setAlignment(Qt::AlignCenter);
    cl->addWidget(lb);
    ly->addWidget(ct);

    ly->addSpacing(12);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto *cancelBtn = new QPushButton("Cancel");
    cancelBtn->setMinimumWidth(90);
    UIHelpers::applyNeutralStyle(cancelBtn);
    connect(cancelBtn, &QPushButton::clicked, d, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    btnLayout->addSpacing(12);

    auto *discardBtn = new QPushButton("Discard");
    discardBtn->setMinimumWidth(90);
    UIHelpers::applyCancelStyle(discardBtn);
    connect(discardBtn, &QPushButton::clicked, d, &QDialog::accept);
    btnLayout->addWidget(discardBtn);

    btnLayout->addStretch();
    ly->addLayout(btnLayout);

    // Center on the ProfileEditor window
    d->adjustSize();
    d->move(geometry().center() - QPoint(d->width() / 2, d->height() / 2));
    int result = d->exec();
    d->deleteLater();

    if (result != QDialog::Accepted) {
      return; // User chose Cancel — stay in editor
    }
  }

  QDialog::reject();
}

void ProfileEditor::mousePressEvent(QMouseEvent *event) {
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

void ProfileEditor::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPos);
    event->accept();
  }
}

void ProfileEditor::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = false;
  }
  QDialog::mouseReleaseEvent(event);
}

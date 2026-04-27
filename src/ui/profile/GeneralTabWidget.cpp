#include "GeneralTabWidget.h"

#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <QRegularExpressionValidator>

#include "core/ThemeManager.h"
#include "ui/ProfileEditor.h" // For AccountProfile, AccountProvider
#include "ui/ToggleSwitch.h"  // For LabeledToggle
#include "ui/UIHelpers.h"

GeneralTabWidget::GeneralTabWidget(AccountProfile &profile, QWidget *parent)
    : QWidget(parent), m_profile(profile) {
  setupUI();
}

void GeneralTabWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);

  // Profile Name
  auto *nameGroup = new QGroupBox("Profile Name");
  auto *nameLayout = new QVBoxLayout(nameGroup);
  m_nicknameEdit = new QLineEdit();
  // Only allow Windows-safe characters: letters, digits, spaces, hyphens, underscores
  m_nicknameEdit->setValidator(
      new QRegularExpressionValidator(
          QRegularExpression(QStringLiteral("[A-Za-z0-9 _-]{1,50}")),
          m_nicknameEdit));
  m_nicknameEdit->setMaxLength(50);
  m_nicknameEdit->setPlaceholderText("e.g., Main Account, Alt Account");
  connect(m_nicknameEdit, &QLineEdit::textChanged, this,
          &GeneralTabWidget::modified);
  nameLayout->addWidget(m_nicknameEdit);
  mainLayout->addWidget(nameGroup);

  // Icon picker with SVG icons
  auto *iconGroup = new QGroupBox("Profile Icon");
  auto *iconOuterLayout = new QVBoxLayout(iconGroup);

  // Selected icon display
  m_iconLabel = new QLabel();
  m_iconLabel->setPixmap(UIHelpers::themedIcon("profile-game").pixmap(32, 32));
  UIHelpers::applyStatusRole(m_iconLabel);
  m_iconLabel->setMinimumSize(50, 50);
  m_iconLabel->setAlignment(Qt::AlignCenter);

  // 20 colorful profile icons in a grid (2 rows of 10)
  QStringList iconPaths = {
      ":/icons/profile-game.svg",    ":/icons/profile-sword.svg",
      ":/icons/profile-shield.svg",  ":/icons/profile-star.svg",
      ":/icons/profile-layers.svg",  ":/icons/profile-sun.svg",
      ":/icons/profile-moon.svg",    ":/icons/profile-alert.svg",
      ":/icons/profile-user.svg",    ":/icons/profile-clock.svg",
      ":/icons/profile-zap.svg",     ":/icons/profile-heart.svg",
      ":/icons/profile-target.svg",  ":/icons/profile-compass.svg",
      ":/icons/profile-hexagon.svg", ":/icons/profile-infinity.svg",
      ":/icons/profile-music.svg",   ":/icons/profile-crescent.svg",
      ":/icons/profile-flame.svg",   ":/icons/profile-stack.svg"};

  auto *iconPickerRow = new QHBoxLayout();
  iconPickerRow->addWidget(m_iconLabel);

  auto *iconGrid = new QGridLayout();
  iconGrid->setSpacing(4);
  int row = 0, col = 0;
  for (const QString &iconPath : iconPaths) {
    auto *btn = new QPushButton();
    btn->setIcon(QIcon(iconPath));
    btn->setIconSize(QSize(20, 20));
    btn->setFixedSize(32, 32);
    UIHelpers::applyNeutralStyle(btn);
    connect(btn, &QPushButton::clicked, [this, iconPath]() {
      m_profile.icon = iconPath;
      m_iconLabel->setPixmap(QIcon(iconPath).pixmap(32, 32));
      emit modified();
    });
    iconGrid->addWidget(btn, row, col);
    col++;
    if (col >= 10) {
      col = 0;
      row++;
    }
  }
  iconPickerRow->addLayout(iconGrid);
  iconPickerRow->addStretch();
  iconOuterLayout->addLayout(iconPickerRow);
  mainLayout->addWidget(iconGroup);

  // Account Provider Group
  auto *accountGroup = new QGroupBox("Account Provider");
  auto *accountLayout = new QVBoxLayout(accountGroup);

  // REVIEW BEFORE BETA: raw QDialog with inline styles — refactor to
  // UIHelpers::createStyledDialog
  // Helper lambda to show Steam/Epic limitations dialog
  auto showProviderLimitations = [this](const QString &provider) {
    QDialog dialog(this);
    dialog.setWindowTitle(provider + " Account Limitations");
    dialog.setMinimumWidth(500);
    auto *layout = new QVBoxLayout(&dialog);

    // Header with SVG icon
    auto *headerRow = new QHBoxLayout();
    auto *headerIcon = new QLabel();
    headerIcon->setPixmap(UIHelpers::themedIcon("alert-yellow").pixmap(20, 20));
    headerRow->addWidget(headerIcon);
    auto *header = new QLabel(provider + " accounts have some limitations:");
    UIHelpers::applyWarningColorRole(header);
    header->setStyleSheet(
        QString("font-size: %1px; font-weight: bold;")
            .arg(ThemeManager::instance().activeTheme().layout.fontSizeNormal));
    headerRow->addWidget(header, 1);
    layout->addLayout(headerRow);

    // Working features (green)
    auto *workingHeader = new QHBoxLayout();
    auto *workingIcon = new QLabel();
    workingIcon->setPixmap(UIHelpers::themedIcon("check-green").pixmap(16, 16));
    workingHeader->addWidget(workingIcon);
    auto *workingTitle = new QLabel("WORKS");
    UIHelpers::applySuccessColorRole(workingTitle);
    workingTitle->setStyleSheet("font-weight: bold;");
    workingHeader->addWidget(workingTitle);
    workingHeader->addStretch();
    layout->addLayout(workingHeader);
    auto *workingList = new QLabel("  • Profile management (nicknames, icons)\n"
                                   "  • DLL/Addon injection (arcdps, etc.)\n"
                                   "  • Window positioning & resizing\n"
                                   "  • Process priority adjustment");
    UIHelpers::applyHintRole(workingList);
    workingList->setStyleSheet(
        QString("padding: %1px 0 %2px %3px;")
            .arg(ThemeManager::instance().activeTheme().layout.paddingSmall)
            .arg(ThemeManager::instance().activeTheme().layout.paddingNormal)
            .arg(ThemeManager::instance().activeTheme().layout.contentIndent));
    layout->addWidget(workingList);

    // May not work (yellow)
    auto *partialHeader = new QHBoxLayout();
    auto *partialIcon = new QLabel();
    partialIcon->setPixmap(
        UIHelpers::themedIcon("alert-yellow").pixmap(16, 16));
    partialHeader->addWidget(partialIcon);
    auto *partialTitle = new QLabel("MAY NOT WORK");
    UIHelpers::applyWarningColorRole(partialTitle);
    partialTitle->setStyleSheet("font-weight: bold;");
    partialHeader->addWidget(partialTitle);
    partialHeader->addStretch();
    layout->addLayout(partialHeader);
    auto *partialList = new QLabel("  • Custom launch arguments (limited by platform)");
    UIHelpers::applyHintRole(partialList);
    partialList->setStyleSheet(
        QString("padding: %1px 0 %2px %3px;")
            .arg(ThemeManager::instance().activeTheme().layout.paddingSmall)
            .arg(ThemeManager::instance().activeTheme().layout.paddingNormal)
            .arg(ThemeManager::instance().activeTheme().layout.contentIndent));
    layout->addWidget(partialList);

    // Not working (red)
    auto *notWorkHeader = new QHBoxLayout();
    auto *notWorkIcon = new QLabel();
    notWorkIcon->setPixmap(UIHelpers::themedIcon("x-circle").pixmap(16, 16));
    notWorkHeader->addWidget(notWorkIcon);
    auto *notWorkTitle = new QLabel("DOES NOT WORK");
    UIHelpers::applyErrorColorRole(notWorkTitle);
    notWorkTitle->setStyleSheet("font-weight: bold;");
    notWorkHeader->addWidget(notWorkTitle);
    notWorkHeader->addStretch();
    layout->addLayout(notWorkHeader);
    auto *notWorkList =
        new QLabel("  • Custom launch arguments (auto-login, network, etc.)\n"
                   "  • Local.dat auto-login (" +
                   provider +
                   " handles authentication)\n"
                   "  • Network server overrides (-authsrv, -portal)");
    UIHelpers::applyHintRole(notWorkList);
    notWorkList->setStyleSheet(
        QString("padding: %1px 0 %2px %3px;")
            .arg(ThemeManager::instance().activeTheme().layout.paddingSmall)
            .arg(ThemeManager::instance().activeTheme().layout.paddingNormal)
            .arg(ThemeManager::instance().activeTheme().layout.contentIndent));
    layout->addWidget(notWorkList);

    // Multiboxing setup section
    auto *multiboxHeader = new QHBoxLayout();
    auto *multiboxIcon = new QLabel();
    multiboxIcon->setPixmap(
        UIHelpers::themedIcon("profile-game").pixmap(16, 16));
    multiboxHeader->addWidget(multiboxIcon);
    auto *multiboxTitle = new QLabel("MULTIBOXING SETUP (One-Time)");
    UIHelpers::applyLabelRole(multiboxTitle);
    multiboxTitle->setStyleSheet("font-weight: bold;");
    multiboxHeader->addWidget(multiboxTitle);
    multiboxHeader->addStretch();
    layout->addLayout(multiboxHeader);

    auto *multiboxGroup = new QWidget();
    auto *multiboxLayout = new QVBoxLayout(multiboxGroup);
    multiboxLayout->setContentsMargins(24, 4, 4, 4);

    auto *multiboxInfo =
        new QLabel("To run Standalone accounts alongside " + provider +
                   ", you need to enable file sharing.\n"
                   "This is a one-time setup in " +
                   provider + ".");
    multiboxInfo->setWordWrap(true);
    UIHelpers::applyHintRole(multiboxInfo);
    multiboxLayout->addWidget(multiboxInfo);

    QString steps;
    if (provider == "Steam") {
      steps = "1. Open Steam\n"
              "2. Right-click Guild Wars 2 → Properties\n"
              "3. In 'Launch Options', paste the text below\n"
              "4. Close Properties - done!";
    } else {
      steps = "1. Open Epic Games Launcher\n"
              "2. Click ⚙ Settings → Guild Wars 2\n"
              "3. Check 'Additional Command Line Arguments'\n"
              "4. Paste the text below → done!";
    }
    auto *stepsLabel = new QLabel(steps);
    UIHelpers::applyStatusRole(stepsLabel);
    stepsLabel->setStyleSheet(
        QString("font-family: '%1';")
            .arg(ThemeManager::instance().activeTheme().layout.fontFamilyMono));
    multiboxLayout->addWidget(stepsLabel);

    // Copy-to-clipboard row
    auto *copyLayout = new QHBoxLayout();
    // Build launch options string with per-profile mumble name
    QString launchArgs = QStringLiteral("-shareArchive -windowed -mumble %1")
                             .arg(m_profile.mumbleLinkName);
    auto *argLabel = new QLabel(launchArgs);
    UIHelpers::applySuccessColorRole(argLabel);
    argLabel->setStyleSheet(
        QString("font-size: %1px; font-weight: bold; padding: %2px; "
                "font-family: '%3';")
            .arg(ThemeManager::instance().activeTheme().layout.fontSizeNormal)
            .arg(ThemeManager::instance().activeTheme().layout.paddingNormal)
            .arg(ThemeManager::instance().activeTheme().layout.fontFamilyMono));
    copyLayout->addWidget(argLabel, 1);

    auto *copyBtn = new QPushButton(" Copy");
    copyBtn->setIcon(UIHelpers::themedIcon("copy"));
    copyBtn->setIconSize(QSize(14, 14));
    UIHelpers::applyActionStyle(copyBtn);
    connect(copyBtn, &QPushButton::clicked, [copyBtn, launchArgs]() {
      QGuiApplication::clipboard()->setText(launchArgs);
      copyBtn->setText(" Copied!");
      copyBtn->setIcon(UIHelpers::themedIcon("check-green"));
    });
    copyLayout->addWidget(copyBtn);
    multiboxLayout->addLayout(copyLayout);

    auto *multiboxNote =
        new QLabel("After this setup, " + provider +
                   "-launched GW2 will share its archive file,\n"
                   "allowing Standalone profiles to run alongside it!");
    multiboxNote->setWordWrap(true);
    UIHelpers::applyHintRole(multiboxNote);
    multiboxLayout->addWidget(multiboxNote);

    layout->addWidget(multiboxGroup);

    // Note about arg removal
    auto *noteLabel = new QLabel("Note: Incompatible arguments will be "
                                 "automatically removed when saving.");
    UIHelpers::applyHintRole(noteLabel);
    noteLabel->setWordWrap(true);
    layout->addWidget(noteLabel);

    // OK button
    auto *okBtn = new QPushButton("I Understand");
    UIHelpers::applyPrimaryStyle(okBtn);
    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(okBtn, 0, Qt::AlignCenter);

    dialog.exec();
  };

  // Three interlocked toggles - only one can be ON at a time
  m_providerStandaloneToggle = new LabeledToggle("Standalone (ArenaNet)");
  m_providerStandaloneToggle->setToolTip(
      "Standard ArenaNet account - no Steam or Epic required");
  accountLayout->addWidget(m_providerStandaloneToggle);
  auto *standaloneNote =
      new QLabel("Purchased directly from ArenaNet (Full features)");
  UIHelpers::applySuccessColorRole(standaloneNote);
  standaloneNote->setStyleSheet(
      QString("font-size: %1px; margin-left: 52px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  accountLayout->addWidget(standaloneNote);

  // Separator
  auto *sep1 = new QFrame();
  sep1->setFrameShape(QFrame::HLine);
  sep1->setStyleSheet("background: palette(mid);");
  sep1->setFixedHeight(1);
  accountLayout->addWidget(sep1);

  // Steam toggle with warning button
  auto *steamRow = new QHBoxLayout();
  m_providerSteamToggle = new LabeledToggle("Steam");
  m_providerSteamToggle->setToolTip(
      "Steam account - requires Steam client to be running");
  steamRow->addWidget(m_providerSteamToggle);
  auto *steamWarnBtn = new QPushButton(" Info");
  steamWarnBtn->setIcon(UIHelpers::themedIcon("alert-yellow"));
  steamWarnBtn->setIconSize(QSize(16, 16));
  steamWarnBtn->setMinimumWidth(75);
  steamWarnBtn->setToolTip("Click to view Steam account limitations");
  UIHelpers::applyCancelStyle(steamWarnBtn);
  connect(steamWarnBtn, &QPushButton::clicked,
          [showProviderLimitations]() { showProviderLimitations("Steam"); });
  steamRow->addWidget(steamWarnBtn);
  steamRow->addStretch();
  accountLayout->addLayout(steamRow);
  auto *steamNote = new QLabel("Limited features - Steam must be running");
  UIHelpers::applyWarningColorRole(steamNote);
  steamNote->setStyleSheet(
      QString("font-size: %1px; margin-left: 52px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  accountLayout->addWidget(steamNote);

  // Separator
  auto *sep2 = new QFrame();
  sep2->setFrameShape(QFrame::HLine);
  sep2->setStyleSheet("background: palette(mid);");
  sep2->setFixedHeight(1);
  accountLayout->addWidget(sep2);

  // Epic toggle with warning button
  auto *epicRow = new QHBoxLayout();
  m_providerEpicToggle = new LabeledToggle("Epic Games");
  m_providerEpicToggle->setToolTip(
      "Epic Games account - requires Epic Games client to be running");
  epicRow->addWidget(m_providerEpicToggle);
  auto *epicWarnBtn = new QPushButton(" Info");
  epicWarnBtn->setIcon(UIHelpers::themedIcon("alert-yellow"));
  epicWarnBtn->setIconSize(QSize(16, 16));
  epicWarnBtn->setMinimumWidth(75);
  epicWarnBtn->setToolTip("Click to view Epic Games account limitations");
  UIHelpers::applyCancelStyle(epicWarnBtn);
  connect(epicWarnBtn, &QPushButton::clicked, [showProviderLimitations]() {
    showProviderLimitations("Epic Games");
  });
  epicRow->addWidget(epicWarnBtn);
  epicRow->addStretch();
  accountLayout->addLayout(epicRow);
  auto *epicNote = new QLabel("Limited features - Epic Games must be running");
  UIHelpers::applyWarningColorRole(epicNote);
  epicNote->setStyleSheet(
      QString("font-size: %1px; margin-left: 52px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  accountLayout->addWidget(epicNote);

  // Interlock toggles - only one can be ON
  connect(m_providerStandaloneToggle, &LabeledToggle::toggled,
          [this](bool checked) {
            if (checked) {
              m_providerSteamToggle->blockSignals(true);
              m_providerEpicToggle->blockSignals(true);
              m_providerSteamToggle->setChecked(false);
              m_providerEpicToggle->setChecked(false);
              m_providerSteamToggle->blockSignals(false);
              m_providerEpicToggle->blockSignals(false);
              emit modified();
            }
          });
  connect(m_providerSteamToggle, &LabeledToggle::toggled, [this](bool checked) {
    if (checked) {
      m_providerStandaloneToggle->blockSignals(true);
      m_providerEpicToggle->blockSignals(true);
      m_providerStandaloneToggle->setChecked(false);
      m_providerEpicToggle->setChecked(false);
      m_providerStandaloneToggle->blockSignals(false);
      m_providerEpicToggle->blockSignals(false);
      emit modified();
    }
  });
  connect(m_providerEpicToggle, &LabeledToggle::toggled, [this](bool checked) {
    if (checked) {
      m_providerStandaloneToggle->blockSignals(true);
      m_providerSteamToggle->blockSignals(true);
      m_providerStandaloneToggle->setChecked(false);
      m_providerSteamToggle->setChecked(false);
      m_providerStandaloneToggle->blockSignals(false);
      m_providerSteamToggle->blockSignals(false);
      emit modified();
    }
  });

  mainLayout->addWidget(accountGroup);

  // Performance
  auto *perfGroup = new QGroupBox("Performance");
  auto *perfLayout = new QFormLayout(perfGroup);
  m_priorityCombo = new QComboBox();
  m_priorityCombo->addItems({"Normal", "Above Normal", "High", "Realtime"});
  // comboBoxStyle removed — handled by global QSS
  connect(m_priorityCombo, &QComboBox::currentIndexChanged, this,
          [this]() { emit modified(); });
  perfLayout->addRow("Process Priority:", m_priorityCombo);
  mainLayout->addWidget(perfGroup);

  // Session History section
  auto *sessionGroup = new QGroupBox("Session History");
  auto *sessionLayout = new QFormLayout(sessionGroup);
  sessionLayout->setLabelAlignment(Qt::AlignRight);

  // Last Login (only field that works reliably)
  QString lastLoginStr =
      m_profile.lastLoginTime.isValid()
          ? m_profile.lastLoginTime.toString("dddd, MMMM d, yyyy h:mm:ss AP")
          : "Never";
  auto *lastLoginLabel = new QLabel(lastLoginStr);
  UIHelpers::applySecondaryRole(lastLoginLabel);
  sessionLayout->addRow("Last Login:", lastLoginLabel);

  mainLayout->addWidget(sessionGroup);

  mainLayout->addStretch();
}

void GeneralTabWidget::load() {
  m_nicknameEdit->blockSignals(true);
  m_nicknameEdit->setText(m_profile.nickname);
  m_nicknameEdit->blockSignals(false);

  // Display icon as pixmap (SVG path or default)
  QString iconPath =
      m_profile.icon.isEmpty() ? ":/icons/profile-game.svg" : m_profile.icon;
  if (iconPath.startsWith(":/icons/")) {
    m_iconLabel->setPixmap(QIcon(iconPath).pixmap(32, 32));
  } else {
    // Legacy or invalid - use default
    m_iconLabel->setPixmap(
        UIHelpers::themedIcon("profile-game").pixmap(32, 32));
  }

  // Set account provider toggles based on saved enum
  m_providerStandaloneToggle->blockSignals(true);
  m_providerSteamToggle->blockSignals(true);
  m_providerEpicToggle->blockSignals(true);

  m_providerStandaloneToggle->setChecked(m_profile.accountProvider ==
                                         AccountProvider::Standalone);
  m_providerSteamToggle->setChecked(m_profile.accountProvider ==
                                    AccountProvider::Steam);
  m_providerEpicToggle->setChecked(m_profile.accountProvider ==
                                   AccountProvider::Epic);

  m_providerStandaloneToggle->blockSignals(false);
  m_providerSteamToggle->blockSignals(false);
  m_providerEpicToggle->blockSignals(false);

  m_priorityCombo->blockSignals(true);
  m_priorityCombo->setCurrentIndex(m_profile.processPriority);
  m_priorityCombo->blockSignals(false);
}

void GeneralTabWidget::save() {
  m_profile.nickname = m_nicknameEdit->text();

  // Save account provider from toggles
  if (m_providerSteamToggle->isChecked()) {
    m_profile.accountProvider = AccountProvider::Steam;
  } else if (m_providerEpicToggle->isChecked()) {
    m_profile.accountProvider = AccountProvider::Epic;
  } else {
    m_profile.accountProvider = AccountProvider::Standalone;
  }

  m_profile.processPriority = m_priorityCombo->currentIndex();
}

QString GeneralTabWidget::getNickname() const {
  return m_nicknameEdit->text().trimmed();
}

bool GeneralTabWidget::isNicknameEmpty() const {
  return m_nicknameEdit->text().trimmed().isEmpty();
}

void GeneralTabWidget::focusNickname() { m_nicknameEdit->setFocus(); }

/**
 * @file SetupWizard.cpp
 * @brief First-run setup wizard - Custom frameless dialog following dev
 * standards
 *
 * Follows UIHelpers pattern: frameless, gold border, dark bg, SVG icons only.
 */

#include "SetupWizard.h"

#include <QDir>
#include <QFileInfo>
#include <QIcon>

#include "UIHelpers.h"

// ====================================
// SetupWizard
// ====================================

SetupWizard::SetupWizard(SettingsManager *settings, QWidget *parent)
    : QDialog(parent), m_settings(settings) {
  setWindowTitle("GW2 AIO Manager Setup");

  // Frameless window following dev standards
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setMinimumSize(650, 500);

  setupUI();
}

void SetupWizard::setupUI() {
  // Outer layout for translucent window
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  // Background container with gold border (dev standard)
  auto *bgContainer = new QWidget();
  bgContainer->setObjectName("wizardBg");
  UIHelpers::applyPopupBackgroundRole(bgContainer);
  outerLayout->addWidget(bgContainer);

  auto *mainLayout = new QVBoxLayout(bgContainer);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // === Custom Title Bar (matches MainWindow) ===
  auto *titleBar = new QWidget();
  UIHelpers::applyTitleBarRole(titleBar);
  titleBar->setFixedHeight(50);
  auto *titleBarLayout = new QHBoxLayout(titleBar);
  titleBarLayout->setContentsMargins(16, 10, 10, 10);

  // App icon
  auto *appIcon = new QLabel();
  UIHelpers::setThemedPixmap(appIcon, "app-icon", 24);
  // REVIEW BEFORE BETA: inline setStyleSheet (uses ThemeManager values)
  appIcon->setStyleSheet("background: transparent; border: none;");
  titleBarLayout->addWidget(appIcon);

  // Title
  m_titleLabel = new QLabel("Setup Wizard");
  UIHelpers::applyGoldColorRole(m_titleLabel);
  // REVIEW BEFORE BETA: inline setStyleSheet (uses ThemeManager values)
  m_titleLabel->setStyleSheet(
      QString("font-size: %1px; font-weight: bold;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeTitle));
  titleBarLayout->addWidget(m_titleLabel);

  titleBarLayout->addStretch();

  // Close button
  auto *closeBtn = new QPushButton();
  UIHelpers::setThemedIcon(closeBtn, "x");
  closeBtn->setIconSize(QSize(14, 14));
  closeBtn->setFixedSize(28, 28);
  closeBtn->setToolTip("Close");
  UIHelpers::applyCancelStyle(closeBtn);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
  titleBarLayout->addWidget(closeBtn);

  mainLayout->addWidget(titleBar);

  // === Content Area ===
  auto *contentWidget = new QWidget();
  UIHelpers::applyWindowBackgroundRole(contentWidget);
  auto *contentLayout = new QVBoxLayout(contentWidget);
  contentLayout->setContentsMargins(30, 30, 30, 20);
  contentLayout->setSpacing(20);

  // Pages
  m_pages = new QStackedWidget();
  m_pages->addWidget(createWelcomePage());
  m_pages->addWidget(createGW2PathPage());
  m_pages->addWidget(createFeaturesPage());
  m_pages->addWidget(createCompletePage());
  contentLayout->addWidget(m_pages, 1);

  // === Navigation Buttons ===
  auto *navLayout = new QHBoxLayout();
  navLayout->setSpacing(12);

  navLayout->addStretch();

  m_backBtn = new QPushButton("Back");
  m_backBtn->setMinimumWidth(100);
  UIHelpers::applyNeutralStyle(m_backBtn);
  connect(m_backBtn, &QPushButton::clicked, this, &SetupWizard::onBackClicked);
  navLayout->addWidget(m_backBtn);

  m_nextBtn = new QPushButton("Next");
  m_nextBtn->setMinimumWidth(100);
  UIHelpers::applyPrimaryStyle(m_nextBtn);
  connect(m_nextBtn, &QPushButton::clicked, this, &SetupWizard::onNextClicked);
  navLayout->addWidget(m_nextBtn);

  m_finishBtn = new QPushButton("Finish");
  m_finishBtn->setMinimumWidth(100);
  UIHelpers::applyConfirmStyle(m_finishBtn);
  connect(m_finishBtn, &QPushButton::clicked, this,
          &SetupWizard::onFinishClicked);
  navLayout->addWidget(m_finishBtn);

  navLayout->addStretch();

  contentLayout->addLayout(navLayout);
  mainLayout->addWidget(contentWidget, 1);

  updateNavigation();
  UIHelpers::centerDialog(this);
}

QWidget *SetupWizard::createWelcomePage() {
  auto *page = new QWidget();
  auto *layout = new QVBoxLayout(page);
  layout->setSpacing(20);
  layout->setAlignment(Qt::AlignCenter);

  // Welcome icon
  auto *iconLabel = new QLabel();
  UIHelpers::setThemedPixmap(iconLabel, "app-icon", 64);
  iconLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(iconLabel);

  // Title
  auto *title = new QLabel("Welcome to GW2 AIO Manager");
  UIHelpers::applyGoldTitleRole(title);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  // Subtitle
  auto *subtitle =
      new QLabel("The ultimate All-In-One companion for Guild Wars 2.\n\n"
                 "This setup will help you configure:\n"
                 "• GW2 installation path\n"
                 "• Feature preferences\n\n"
                 "Click Next to continue.");
  UIHelpers::applyPopupLabelRole(subtitle);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setWordWrap(true);
  layout->addWidget(subtitle);

  layout->addStretch();
  return page;
}

QWidget *SetupWizard::createGW2PathPage() {
  auto *page = new QWidget();
  auto *layout = new QVBoxLayout(page);
  layout->setSpacing(16);

  // Title
  auto *title = new QLabel("Locate Guild Wars 2");
  UIHelpers::applyGoldTitleRole(title);
  layout->addWidget(title);

  // Description
  auto *desc =
      new QLabel("Enter the path to your Guild Wars 2 installation folder,\n"
                 "or use Auto-Detect to find it automatically.");
  UIHelpers::applyPopupLabelRole(desc);
  desc->setWordWrap(true);
  layout->addWidget(desc);

  layout->addSpacing(10);

  // Path input
  auto *pathLayout = new QHBoxLayout();
  m_pathEdit = new QLineEdit();
  m_pathEdit->setPlaceholderText("C:\\Program Files\\Guild Wars 2");
  UIHelpers::applyInputFieldRole(m_pathEdit);
  m_pathEdit->setMinimumHeight(40);
  pathLayout->addWidget(m_pathEdit, 1);

  auto *browseBtn = new QPushButton();
  UIHelpers::setThemedIcon(browseBtn, "folder");
  browseBtn->setIconSize(QSize(18, 18));
  browseBtn->setFixedSize(44, 44);
  browseBtn->setToolTip("Browse...");
  UIHelpers::applyNeutralStyle(browseBtn);
  connect(browseBtn, &QPushButton::clicked, this, [this]() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Guild Wars 2 Folder", m_pathEdit->text());
    if (!dir.isEmpty()) {
      m_pathEdit->setText(dir);
    }
  });
  pathLayout->addWidget(browseBtn);

  layout->addLayout(pathLayout);

  // Auto-detect button
  auto *autoDetectBtn = new QPushButton();
  UIHelpers::setThemedIcon(autoDetectBtn, "target");
  autoDetectBtn->setText(" Auto-Detect");
  autoDetectBtn->setMinimumHeight(40);
  UIHelpers::applyNeutralStyle(autoDetectBtn);
  connect(autoDetectBtn, &QPushButton::clicked, this, [this]() {
    QString detected = m_detector.detectGW2Path();
    if (!detected.isEmpty()) {
      m_pathEdit->setText(detected);
      m_pathStatusLabel->setText("✓ Found GW2 installation");
      UIHelpers::applySuccessColorRole(m_pathStatusLabel);
    } else {
      m_pathStatusLabel->setText("Could not auto-detect. Please browse.");
      UIHelpers::applyWarningColorRole(m_pathStatusLabel);
    }
  });
  layout->addWidget(autoDetectBtn);

  // Status label
  m_pathStatusLabel = new QLabel("");
  UIHelpers::applyStatusRole(m_pathStatusLabel);
  layout->addWidget(m_pathStatusLabel);

  layout->addStretch();

  // Register field for wizard
  connect(m_pathEdit, &QLineEdit::textChanged, this,
          [this](const QString &text) { m_gw2Path = text; });

  return page;
}

QWidget *SetupWizard::createFeaturesPage() {
  auto *page = new QWidget();
  auto *layout = new QVBoxLayout(page);
  layout->setSpacing(16);

  // Title
  auto *title = new QLabel("Some of Our Features");
  UIHelpers::applyGoldTitleRole(title);
  layout->addWidget(title);

  // Description
  auto *desc = new QLabel("GW2 AIO Manager includes everything you need\n"
                          "to enhance your Guild Wars 2 experience:");
  UIHelpers::applyPopupLabelRole(desc);
  layout->addWidget(desc);

  layout->addSpacing(10);

  // Feature display (info only, no toggles)
  auto createFeatureRow = [layout](const QString &iconPath, const QString &name,
                                   const QString &description) {
    auto *row = new QWidget();
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(8, 8, 8, 8);
    rowLayout->setSpacing(16);

    auto *icon = new QLabel();
    icon->setPixmap(QIcon(iconPath).pixmap(32, 32));
    icon->setFixedSize(32, 32);
    rowLayout->addWidget(icon);

    auto *textLayout = new QVBoxLayout();
    textLayout->setSpacing(2);

    auto *nameLabel = new QLabel(name);
    UIHelpers::applyGoldColorRole(nameLabel);
    // REVIEW BEFORE BETA: inline setStyleSheet (uses ThemeManager values)
    nameLabel->setStyleSheet(
        QString("font-weight: bold; font-size: %1px;")
            .arg(ThemeManager::instance().activeTheme().layout.fontSizeNormal));
    textLayout->addWidget(nameLabel);

    auto *descLabel = new QLabel(description);
    UIHelpers::applyHintRole(descLabel);
    textLayout->addWidget(descLabel);

    rowLayout->addLayout(textLayout, 1);
    layout->addWidget(row);
  };

  createFeatureRow(":/icons/layers.svg", "Radial Menus",
                   "Quick access to mounts, novelties & more");
  createFeatureRow(":/icons/profile-flame.svg", "DPS Tracker",
                   "Combat statistics and performance graphs");
  createFeatureRow(":/icons/profile-compass.svg", "Marker System",
                   "TacO-compatible waypoints and routes");
  createFeatureRow(":/icons/grid.svg", "Blish-HUD Modules",
                   "Use the existing module library");

  layout->addStretch();
  return page;
}

QWidget *SetupWizard::createCompletePage() {
  auto *page = new QWidget();
  auto *layout = new QVBoxLayout(page);
  layout->setSpacing(20);
  layout->setAlignment(Qt::AlignCenter);

  // Success icon
  auto *iconLabel = new QLabel();
  UIHelpers::setThemedPixmap(iconLabel, "check-circle", 64);
  iconLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(iconLabel);

  // Title
  auto *title = new QLabel("Setup Complete!");
  UIHelpers::applyGoldTitleRole(title);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  // Summary
  auto *summary = new QLabel("GW2 AIO Manager is ready to use.\n\n"
                             "You can now create profiles, manage addons,\n"
                             "and launch Guild Wars 2 with custom settings.\n\n"
                             "Click Finish to start using the application.");
  UIHelpers::applyPopupLabelRole(summary);
  summary->setAlignment(Qt::AlignCenter);
  summary->setWordWrap(true);
  layout->addWidget(summary);

  layout->addStretch();
  return page;
}

void SetupWizard::updateNavigation() {
  int currentPage = m_pages->currentIndex();
  int lastPage = m_pages->count() - 1;

  m_backBtn->setVisible(currentPage > 0);
  m_nextBtn->setVisible(currentPage < lastPage);
  m_finishBtn->setVisible(currentPage == lastPage);

  // Update title based on page
  QStringList pageTitles = {"Welcome", "GW2 Location", "Features", "Complete"};
  if (currentPage < pageTitles.size()) {
    m_titleLabel->setText("Setup Wizard - " + pageTitles[currentPage]);
  }
}

void SetupWizard::onNextClicked() {
  // Validate current page before advancing
  if (m_pages->currentIndex() == 1) { // GW2 Path page
    if (m_pathEdit->text().isEmpty()) {
      m_pathStatusLabel->setText("Please enter a GW2 path");
      UIHelpers::applyErrorColorRole(m_pathStatusLabel);
      return;
    }
    // Validate path exists
    QFileInfo info(m_pathEdit->text());
    if (!info.exists() || !info.isDir()) {
      m_pathStatusLabel->setText("Invalid path");
      UIHelpers::applyErrorColorRole(m_pathStatusLabel);
      return;
    }
    m_gw2Path = m_pathEdit->text();
  }

  if (m_pages->currentIndex() < m_pages->count() - 1) {
    m_pages->setCurrentIndex(m_pages->currentIndex() + 1);
    updateNavigation();
  }
}

void SetupWizard::onBackClicked() {
  if (m_pages->currentIndex() > 0) {
    m_pages->setCurrentIndex(m_pages->currentIndex() - 1);
    updateNavigation();
  }
}

void SetupWizard::onFinishClicked() {
  // Save settings - all features enabled by default (AIO = All-In-One)
  m_settings->setValue("general/gw2Path", m_gw2Path);
  m_settings->setValue("radial/enabled", true);
  m_settings->setValue("dps/enabled", false);
  m_settings->setValue("markers/enabled", true);
  m_settings->setValue("modules/enabled", true);
  m_settings->setValue("general/setupComplete", true);
  m_settings->sync();

  accept();
}

// === Drag Handling (same as MainWindow) ===

void SetupWizard::mousePressEvent(QMouseEvent *event) {
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

void SetupWizard::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPos);
    event->accept();
  }
}

void SetupWizard::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = false;
  }
  QDialog::mouseReleaseEvent(event);
}

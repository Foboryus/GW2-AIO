/**
 * @file MarkerPackBrowser.cpp
 * @brief Two-column split panel for online + installed marker packs
 *
 * Layout:
 * - Page header with Auto-Update toggle + Check for Updates button
 * - Top: QSplitter with Installed (left) / Available (right) cards
 * - Bottom: Tree view of pack categories (existing functionality)
 *
 * All colors from ThemeManager tokens — no inline hex values.
 *
 * DO NOT ADD:
 * - Inline styles (use UIHelpers / ThemeManager tokens)
 * - Rendering logic (belongs in renderers)
 */

#include "MarkerPackBrowser.h"
#include "ToggleSwitch.h"
#include "UIHelpers.h"
#include "core/DataService.h"
#include "core/SettingsManager.h"
#include "core/ThemeManager.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerController.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerModels.h"
#include "features/markers/MarkerPackRegistry.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QMimeData>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <numeric>

// ============================================================================
// Constructor
// ============================================================================

MarkerPackBrowser::MarkerPackBrowser(DataService *dataService,
                                     MarkerController *markerController,
                                     QWidget *parent)
    : QWidget(parent), m_dataService(dataService),
      m_markerController(markerController) {
  if (m_markerController) {
    m_registry = m_markerController->registry();
  }

  setupUI();

  // Deferred initial refresh
  QTimer::singleShot(0, this, &MarkerPackBrowser::refreshTree);

  // Connect registry signals
  if (m_registry) {
    connect(m_registry, &MarkerPackRegistry::packStatusChanged, this,
            &MarkerPackBrowser::onPackStatusChanged);

    connect(m_registry, &MarkerPackRegistry::updateCheckComplete, this,
            [this]() {
              if (m_checkUpdatesBtn) {
                m_checkUpdatesBtn->setEnabled(true);
                m_checkUpdatesBtn->setText("Check for Updates");
              }
              rebuildCards();
            });

    connect(m_registry, &MarkerPackRegistry::manifestLoaded, this, [this]() {
      rebuildCards();
      // Auto-check for updates after manifest loads
      if (m_registry) {
        m_registry->checkForUpdates();
      }
    });

    connect(m_registry, &MarkerPackRegistry::manifestFetchFailed, this,
            [this](const QString &error) {
              Q_UNUSED(error);
              // Silently re-enable — no popup for network issues
              if (m_checkUpdatesBtn) {
                m_checkUpdatesBtn->setEnabled(true);
                m_checkUpdatesBtn->setText("Check for Updates");
              }
            });
  }
}

// ============================================================================
// UI Setup
// ============================================================================

void MarkerPackBrowser::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(8);
  setAcceptDrops(false); // Win32 DragAcceptFiles in MainWindow handles drops

  // Page header
  auto *header = UIHelpers::createPageHeader(this, "Marker Packs", "compass");
  auto *headerLayout = qobject_cast<QHBoxLayout *>(header->layout());

  // Auto-Update toggle in header
  m_autoUpdateToggle = new LabeledToggle("Auto-Update All", this);
  bool autoUpdateDefault = true;
  if (m_dataService && m_dataService->settingsManager()) {
    autoUpdateDefault = m_dataService->settingsManager()
                            ->value("markerPacks/autoUpdate", true)
                            .toBool();
  }
  m_autoUpdateToggle->toggle()->setChecked(autoUpdateDefault);
  connect(m_autoUpdateToggle, &LabeledToggle::toggled, this, [this](bool on) {
    if (m_dataService && m_dataService->settingsManager()) {
      m_dataService->settingsManager()->setValue("markerPacks/autoUpdate", on);
    }
    rebuildCards();
  });

  // Check for Updates button
  m_checkUpdatesBtn = new QPushButton("Check for Updates");
  UIHelpers::setThemedIcon(m_checkUpdatesBtn, "refresh");
  UIHelpers::applyNeutralStyle(m_checkUpdatesBtn);
  m_checkUpdatesBtn->setMinimumHeight(28);
  connect(m_checkUpdatesBtn, &QPushButton::clicked, this, [this]() {
    if (m_registry) {
      m_checkUpdatesBtn->setEnabled(false);
      m_checkUpdatesBtn->setText("Checking...");

      // Try manifest refresh first; if cooldown blocks it, just check pack
      // versions directly. manifestLoaded auto-triggers checkForUpdates.
      if (!m_registry->fetchRemoteManifest()) {
        m_registry->checkForUpdates();
      }
    }
  });

  if (headerLayout) {
    headerLayout->addWidget(m_autoUpdateToggle);
    headerLayout->addWidget(m_checkUpdatesBtn);
  }
  mainLayout->addWidget(header);

  // Build the online packs section
  buildOnlinePacksSection(mainLayout);

  // Action bar for local packs
  auto *actionBar = new QHBoxLayout();
  actionBar->setContentsMargins(12, 4, 12, 0);

  m_openFolderBtn = new QPushButton("Open Packs Folder");
  UIHelpers::setThemedIcon(m_openFolderBtn, "folder");
  UIHelpers::applyNeutralStyle(m_openFolderBtn);
  m_openFolderBtn->setMinimumHeight(32);

  m_reloadBtn = new QPushButton("Reload Packs");
  UIHelpers::setThemedIcon(m_reloadBtn, "refresh");
  UIHelpers::applyNeutralStyle(m_reloadBtn);
  m_reloadBtn->setMinimumHeight(32);

  m_statusLabel = new QLabel("No packs loaded");
  UIHelpers::applyHintRole(m_statusLabel);

  actionBar->addWidget(m_openFolderBtn);
  actionBar->addWidget(m_reloadBtn);
  actionBar->addStretch();
  actionBar->addWidget(m_statusLabel);

  mainLayout->addLayout(actionBar);

  // Tree widget for pack/category hierarchy
  m_tree = new QTreeWidget();
  m_tree->setHeaderLabels({"Pack / Category", "Markers"});
  m_tree->setRootIsDecorated(true);
  m_tree->setAnimated(true);
  m_tree->setAlternatingRowColors(false);
  m_tree->setSelectionMode(QAbstractItemView::NoSelection);
  m_tree->setFocusPolicy(Qt::NoFocus);
  m_tree->header()->setStretchLastSection(false);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_tree->setIndentation(20);

  mainLayout->addWidget(m_tree, 2); // 40% of space (vs splitter's stretch 3)

  // Connect actions
  connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
    if (m_markerController) {
      QString packsDir = m_markerController->packsPath();
      QDesktopServices::openUrl(QUrl::fromLocalFile(packsDir));
    }
  });

  connect(m_reloadBtn, &QPushButton::clicked, this, [this]() {
    if (m_markerController) {
      m_statusLabel->setText("Reloading...");
      m_markerController->reloadPacks();
    }
  });

  // Listen for pack load completion
  if (m_markerController) {
    connect(
        m_markerController, &MarkerController::packsLoaded, this,
        [this](int packCount, int markerCount) {
          m_statusLabel->setText(
              QString("%1 packs, %2 markers").arg(packCount).arg(markerCount));
          refreshTree();
          // Also refresh installed cards (files may have been added/removed)
          if (m_registry) {
            m_registry->refreshInstallStatus();
          }
          rebuildCards();
        });
  }
}

// ============================================================================
// Online Packs Section (Two-Column Split)
// ============================================================================

void MarkerPackBrowser::buildOnlinePacksSection(QVBoxLayout *mainLayout) {
  const auto &theme = ThemeManager::instance().activeTheme();
  const auto &w = theme.widgets;
  const auto &c = theme.colors;

  // Side-by-side panels (installed | available)
  auto *panelsRow = new QHBoxLayout();
  panelsRow->setSpacing(8);

  // --- Left panel: INSTALLED ---
  auto *installedPanel = new QWidget();
  auto *installedOuterLayout = new QVBoxLayout(installedPanel);
  installedOuterLayout->setContentsMargins(8, 4, 4, 4);
  installedOuterLayout->setSpacing(4);

  m_installedLabel = new QLabel("INSTALLED");
  m_installedLabel->setStyleSheet(
      QString("color: %1; font-weight: bold; font-size: 12px; padding: 4px;")
          .arg(w.packSectionLabel));
  installedOuterLayout->addWidget(m_installedLabel);

  auto *installedScroll = new QScrollArea();
  installedScroll->setWidgetResizable(true);
  installedScroll->setFrameShape(QFrame::NoFrame);
  installedScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  installedScroll->setStyleSheet(
      QString("QScrollArea { background: transparent; border: none; }"));

  m_installedContainer = new QWidget();
  m_installedLayout = new QVBoxLayout(m_installedContainer);
  m_installedLayout->setContentsMargins(0, 0, 0, 0);
  m_installedLayout->setSpacing(6);
  m_installedLayout->addStretch();
  installedScroll->setWidget(m_installedContainer);

  // Drop hint
  m_dropHintLabel =
      new QLabel("Drag .taco, .zip or .aiomt files here to install");
  m_dropHintLabel->setAlignment(Qt::AlignCenter);
  m_dropHintLabel->setStyleSheet(
      QString("color: %1; font-size: 11px; font-style: italic; padding: 6px; "
              "border: 1px dashed %2; border-radius: 4px; margin: 4px;")
          .arg(c.textHint, w.packCardBorder));
  installedOuterLayout->addWidget(m_dropHintLabel);

  installedOuterLayout->addWidget(installedScroll, 1);
  panelsRow->addWidget(installedPanel, 1);

  // --- Right panel: AVAILABLE ---
  auto *availablePanel = new QWidget();
  auto *availableOuterLayout = new QVBoxLayout(availablePanel);
  availableOuterLayout->setContentsMargins(4, 4, 8, 4);
  availableOuterLayout->setSpacing(4);

  m_availableLabel = new QLabel("AVAILABLE");
  m_availableLabel->setStyleSheet(
      QString("color: %1; font-weight: bold; font-size: 12px; padding: 4px;")
          .arg(w.packSectionLabel));
  availableOuterLayout->addWidget(m_availableLabel);

  auto *availableScroll = new QScrollArea();
  availableScroll->setWidgetResizable(true);
  availableScroll->setFrameShape(QFrame::NoFrame);
  availableScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  availableScroll->setStyleSheet(
      QString("QScrollArea { background: transparent; border: none; }"));

  m_availableContainer = new QWidget();
  m_availableLayout = new QVBoxLayout(m_availableContainer);
  m_availableLayout->setContentsMargins(0, 0, 0, 0);
  m_availableLayout->setSpacing(6);
  m_availableLayout->addStretch();
  availableScroll->setWidget(m_availableContainer);

  availableOuterLayout->addWidget(availableScroll, 1);
  panelsRow->addWidget(availablePanel, 1);

  mainLayout->addLayout(panelsRow, 3); // 60% of space (vs tree's stretch 2)

  // Initial card population
  QTimer::singleShot(0, this, &MarkerPackBrowser::rebuildCards);
}

// ============================================================================
// Card Creation
// ============================================================================

QWidget *MarkerPackBrowser::createPackCard(const OnlineMarkerPack &pack,
                                           bool installed) {
  const auto &theme = ThemeManager::instance().activeTheme();
  const auto &w = theme.widgets;
  const auto &c = theme.colors;

  auto *card = new QWidget();
  card->setObjectName("packCard_" + pack.id);
  card->setStyleSheet(QString("QWidget#packCard_%1 {"
                              "  background: %2;"
                              "  border: 1px solid %3;"
                              "  border-radius: %4px;"
                              "  padding: 8px;"
                              "}"
                              "QWidget#packCard_%1:hover {"
                              "  border-color: %5;"
                              "}")
                          .arg(pack.id, w.packCardBg, w.packCardBorder)
                          .arg(theme.layout.borderRadius)
                          .arg(w.packCardHoverBorder));

  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(3);

  // Pack name (bold)
  auto *nameLabel = new QLabel(pack.name);
  nameLabel->setStyleSheet(
      QString("color: %1; font-weight: bold; font-size: 13px; border: none; "
              "background: transparent;")
          .arg(c.textPrimary));
  layout->addWidget(nameLabel);

  // Author
  if (!pack.author.isEmpty()) {
    auto *authorLabel = new QLabel(QString("by %1").arg(pack.author));
    authorLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; border: none; background: "
                "transparent;")
            .arg(c.textHint));
    layout->addWidget(authorLabel);
  }

  // Description
  if (!pack.description.isEmpty()) {
    auto *descLabel = new QLabel(pack.description);
    descLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; border: none; background: "
                "transparent;")
            .arg(c.textSecondary));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);
  }

  // Status badge (installed packs only)
  if (installed && pack.status == OnlineMarkerPack::UpdateAvailable) {
    auto *statusRow = new QHBoxLayout();
    statusRow->setSpacing(4);
    statusRow->setContentsMargins(0, 0, 0, 0);
    auto *warnIcon = new QLabel();
    warnIcon->setPixmap(
        ThemeManager::instance().icon("alert-triangle").pixmap(14, 14));
    warnIcon->setFixedSize(14, 14);
    warnIcon->setStyleSheet("border: none; background: transparent;");
    statusRow->addWidget(warnIcon);
    auto *statusLabel = new QLabel("Update Available");
    statusLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold; border: none; "
                "background: transparent;")
            .arg(c.warning));
    statusRow->addWidget(statusLabel);
    statusRow->addStretch();
    layout->addLayout(statusRow);
  }

  // Progress bar (during download)
  if (pack.status == OnlineMarkerPack::Downloading) {
    auto *progress = new QProgressBar();
    progress->setRange(0, 100);
    progress->setValue(pack.downloadProgress);
    progress->setTextVisible(true);
    progress->setFixedHeight(18);
    progress->setStyleSheet(
        QString("QProgressBar {"
                "  background: %1; border: 1px solid %2; border-radius: 4px;"
                "  text-align: center; color: %3; font-size: 10px;"
                "}"
                "QProgressBar::chunk {"
                "  background: %4; border-radius: 3px;"
                "}")
            .arg(w.packProgressBg, w.packCardBorder, c.textPrimary,
                 w.packProgressFill));
    progress->setObjectName("progress_" + pack.id);
    layout->addWidget(progress);
  }

  // Error message
  if (pack.status == OnlineMarkerPack::Error && !pack.errorMessage.isEmpty()) {
    auto *errorLabel = new QLabel(pack.errorMessage);
    errorLabel->setStyleSheet(
        QString("color: %1; font-size: 10px; border: none; background: "
                "transparent;")
            .arg(c.error));
    errorLabel->setWordWrap(true);
    layout->addWidget(errorLabel);
  }

  // Button row
  auto *btnRow = new QHBoxLayout();
  btnRow->setSpacing(6);

  if (installed) {
    // Update button (always visible when update is available)
    if (pack.status == OnlineMarkerPack::UpdateAvailable) {
      auto *updateBtn = new QPushButton("Update");
      UIHelpers::setThemedIcon(updateBtn, "refresh");
      UIHelpers::applyRole(updateBtn, "action");
      updateBtn->setMinimumHeight(26);
      QString packId = pack.id;
      connect(updateBtn, &QPushButton::clicked, this, [this, packId]() {
        if (m_registry) {
          m_registry->downloadPack(packId);
        }
      });
      btnRow->addWidget(updateBtn);
    }

    // Source link
    if (!pack.sourceUrl.isEmpty()) {
      auto *sourceBtn = new QPushButton("Source");
      UIHelpers::setThemedIcon(sourceBtn, "external-link");
      UIHelpers::applyNeutralStyle(sourceBtn);
      sourceBtn->setMinimumHeight(26);
      QString url = pack.sourceUrl;
      connect(sourceBtn, &QPushButton::clicked, this,
              [url]() { QDesktopServices::openUrl(QUrl(url)); });
      btnRow->addWidget(sourceBtn);
    }

    btnRow->addStretch();

    // Delete button
    auto *deleteBtn = new QPushButton("Delete");
    UIHelpers::setThemedIcon(deleteBtn, "trash");
    UIHelpers::applyRole(deleteBtn, "cancel");
    deleteBtn->setMinimumHeight(26);
    QString packId = pack.id;
    QString packName = pack.name;
    connect(deleteBtn, &QPushButton::clicked, this, [this, packId, packName]() {
      // Styled confirmation dialog (follows showInfoDialog pattern)
      auto *dlg = UIHelpers::createStyledDialog(this, 380);
      auto *ol = new QVBoxLayout(dlg);
      ol->setContentsMargins(0, 0, 0, 0);
      auto *bg = new QWidget();
      UIHelpers::applyPopupBackgroundRole(bg);
      ol->addWidget(bg);
      auto *ly = new QVBoxLayout(bg);
      ly->setContentsMargins(20, 20, 20, 20);
      ly->setSpacing(12);

      auto *titleLabel = new QLabel("Delete Pack");
      UIHelpers::applyGoldTitleRole(titleLabel);
      titleLabel->setAlignment(Qt::AlignCenter);
      ly->addWidget(titleLabel);

      auto *msgLabel =
          new QLabel(QString("Delete \"%1\"?\n\nThis removes the pack file. "
                             "You can re-download it anytime.")
                         .arg(packName));
      UIHelpers::applyPopupLabelRole(msgLabel);
      msgLabel->setAlignment(Qt::AlignCenter);
      msgLabel->setWordWrap(true);
      ly->addWidget(msgLabel);

      auto *btnLayout = new QHBoxLayout();
      btnLayout->setSpacing(8);
      auto *cancelBtn = new QPushButton("Cancel");
      UIHelpers::applyNeutralStyle(cancelBtn);
      cancelBtn->setMinimumHeight(36);
      auto *confirmBtn = new QPushButton("Delete");
      UIHelpers::applyCancelStyle(confirmBtn);
      confirmBtn->setMinimumHeight(36);
      btnLayout->addStretch();
      btnLayout->addWidget(cancelBtn);
      btnLayout->addWidget(confirmBtn);
      ly->addLayout(btnLayout);

      connect(confirmBtn, &QPushButton::clicked, dlg, [this, dlg, packId]() {
        if (m_registry) {
          m_registry->deletePack(packId);
          if (m_markerController) {
            m_markerController->reloadPacks();
          }
        }
        dlg->accept();
      });
      connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
      UIHelpers::centerDialog(dlg);
      dlg->exec();
      dlg->deleteLater();
      // Force UI refresh after modal closes (signal may have fired during exec)
      rebuildCards();
      refreshTree();
    });
    btnRow->addWidget(deleteBtn);

  } else {
    // Available pack — Download button
    if (pack.status != OnlineMarkerPack::Downloading) {
      auto *downloadBtn = new QPushButton("Download");
      UIHelpers::setThemedIcon(downloadBtn, "download");
      UIHelpers::applyRole(downloadBtn, "primary");
      downloadBtn->setMinimumHeight(26);
      QString packId = pack.id;
      connect(downloadBtn, &QPushButton::clicked, this, [this, packId]() {
        if (m_registry) {
          m_registry->downloadPack(packId);
        }
      });
      btnRow->addWidget(downloadBtn);
    }

    // Source link
    if (!pack.sourceUrl.isEmpty()) {
      auto *sourceBtn = new QPushButton("Source");
      UIHelpers::setThemedIcon(sourceBtn, "external-link");
      UIHelpers::applyNeutralStyle(sourceBtn);
      sourceBtn->setMinimumHeight(26);
      QString url = pack.sourceUrl;
      connect(sourceBtn, &QPushButton::clicked, this,
              [url]() { QDesktopServices::openUrl(QUrl(url)); });
      btnRow->addWidget(sourceBtn);
    }

    btnRow->addStretch();
  }

  layout->addLayout(btnRow);

  return card;
}

// ============================================================================
// Rebuild Cards
// ============================================================================

void MarkerPackBrowser::rebuildCards() {
  if (!m_registry || !m_installedLayout || !m_availableLayout) {
    return;
  }

  // Clear existing cards (keep the stretch at end)
  auto clearLayout = [](QVBoxLayout *layout) {
    while (layout->count() > 1) { // Keep the trailing stretch
      QLayoutItem *item = layout->takeAt(0);
      if (item->widget()) {
        delete item->widget();
      }
      delete item;
    }
  };

  clearLayout(m_installedLayout);
  clearLayout(m_availableLayout);

  // Collect manifest filenames to detect user-installed packs
  QSet<QString> manifestFilenames;
  const auto &packs = m_registry->packs();
  for (const OnlineMarkerPack &pack : packs) {
    manifestFilenames.insert(pack.filename.toLower());
    bool isInstalled = (pack.status == OnlineMarkerPack::Installed ||
                        pack.status == OnlineMarkerPack::UpdateAvailable);

    QWidget *card = createPackCard(pack, isInstalled);

    if (isInstalled) {
      m_installedLayout->insertWidget(m_installedLayout->count() - 1, card);
    } else {
      m_availableLayout->insertWidget(m_availableLayout->count() - 1, card);
    }
  }

  // Detect user-installed packs (files not in manifest)
  if (m_registry) {
    QDir dir(m_registry->packsPath());
    QStringList localFiles =
        dir.entryList({"*.taco", "*.zip", "*.aiomt"}, QDir::Files);
    for (const QString &file : localFiles) {
      if (!manifestFilenames.contains(file.toLower())) {
        QWidget *card = createUserPackCard(file);
        m_installedLayout->insertWidget(m_installedLayout->count() - 1, card);
      }
    }
  }
}

// ============================================================================
// Status Change Handler
// ============================================================================

void MarkerPackBrowser::onPackStatusChanged(const QString &packId) {
  // Full rebuild — simplest approach, cards are lightweight
  rebuildCards();
}

// ============================================================================
// Theme Change Handler
// ============================================================================

void MarkerPackBrowser::changeEvent(QEvent *event) {
  if (event->type() == QEvent::StyleChange) {
    updateSectionStyles();
    // IMPORTANT: Defer rebuildCards to the next event loop iteration.
    // During StyleChange, Qt is iterating all widgets for re-polishing.
    // Destroying/creating widgets synchronously here corrupts Qt's
    // internal widget traversal list → crash.
    QTimer::singleShot(0, this, &MarkerPackBrowser::rebuildCards);
  }
  QWidget::changeEvent(event);
}

void MarkerPackBrowser::updateSectionStyles() {
  const auto &theme = ThemeManager::instance().activeTheme();
  const auto &w = theme.widgets;

  QString sectionStyle =
      QString("color: %1; font-weight: bold; font-size: 12px; padding: 4px;")
          .arg(w.packSectionLabel);

  if (m_installedLabel) {
    m_installedLabel->setStyleSheet(sectionStyle);
  }
  if (m_availableLabel) {
    m_availableLabel->setStyleSheet(sectionStyle);
  }
  // No splitter to re-style (replaced with QHBoxLayout)
}

void MarkerPackBrowser::handleDroppedFiles(const QList<QUrl> &urls) {
  installDroppedFiles(urls);
}

void MarkerPackBrowser::installDroppedFiles(const QList<QUrl> &urls) {
  if (!m_registry) {
    return;
  }

  QString packsDir = m_registry->packsPath();
  int installed = 0;

  for (const QUrl &url : urls) {
    QString srcPath = url.toLocalFile();
    if (srcPath.isEmpty()) {
      continue;
    }

    QFileInfo fi(srcPath);
    QString ext = fi.suffix().toLower();
    if (ext != "taco" && ext != "zip" && ext != "aiomt") {
      continue;
    }

    QString destPath = QDir(packsDir).filePath(fi.fileName());

    // Skip if already exists
    if (QFile::exists(destPath)) {
      qInfo() << "MarkerPackBrowser: Skipping" << fi.fileName()
              << "— already exists";
      continue;
    }

    if (QFile::copy(srcPath, destPath)) {
      qInfo() << "MarkerPackBrowser: Installed" << fi.fileName();
      ++installed;
    } else {
      qWarning() << "MarkerPackBrowser: Failed to copy" << srcPath << "to"
                 << destPath;
    }
  }

  if (installed > 0) {
    // Reload everything
    if (m_markerController) {
      m_markerController->reloadPacks();
    }
    m_registry->refreshInstallStatus();
    rebuildCards();
    refreshTree();

    // Show success notification
    QString msg =
        installed == 1
            ? "1 marker pack installed successfully."
            : QString("%1 marker packs installed successfully.").arg(installed);
    UIHelpers::showInfoDialog(this, msg);
  }
}

// ============================================================================
// User-Installed Pack Card (not from manifest — no Update/Source buttons)
// ============================================================================

QWidget *MarkerPackBrowser::createUserPackCard(const QString &filename) {
  const auto &theme = ThemeManager::instance().activeTheme();
  const auto &w = theme.widgets;
  const auto &c = theme.colors;

  // Sanitize filename for CSS #id selector — keep only [A-Za-z0-9_]
  // Characters like () in "pack (1).taco" break QSS selectors silently,
  // causing the entire card stylesheet to fail (no background, no hover).
  QString safeId;
  for (QChar ch : filename) {
    if (ch.isLetterOrNumber() || ch == '_')
      safeId += ch;
    else
      safeId += '_';
  }

  auto *card = new QWidget();
  card->setObjectName("userCard_" + safeId);
  card->setStyleSheet(QString("QWidget#userCard_%1 {"
                              "  background: %2;"
                              "  border: 1px solid %3;"
                              "  border-radius: %4px;"
                              "  padding: 8px;"
                              "}"
                              "QWidget#userCard_%1:hover {"
                              "  border-color: %5;"
                              "}")
                          .arg(safeId, w.userCardBg, w.userCardBorder)
                          .arg(theme.layout.borderRadius)
                          .arg(w.packCardHoverBorder));

  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(3);

  // Filename as title (same style as createPackCard name label)
  auto *nameLabel = new QLabel(filename);
  nameLabel->setStyleSheet(
      QString("color: %1; font-weight: bold; font-size: 13px; border: none; "
              "background: transparent;")
          .arg(c.textPrimary));
  layout->addWidget(nameLabel);

  // "User Installed" subtitle (same style as author label in createPackCard)
  auto *subtitleLabel = new QLabel("User Installed");
  subtitleLabel->setStyleSheet(
      QString("color: %1; font-size: 11px; font-style: italic; border: none; "
              "background: transparent;")
          .arg(c.textHint));
  layout->addWidget(subtitleLabel);

  // Delete button only (same pattern as installed pack delete)
  auto *btnRow = new QHBoxLayout();
  btnRow->setSpacing(6);
  btnRow->addStretch();

  auto *deleteBtn = new QPushButton("Delete");
  UIHelpers::setThemedIcon(deleteBtn, "trash");
  UIHelpers::applyRole(deleteBtn, "cancel");
  deleteBtn->setMinimumHeight(26);
  deleteBtn->setCursor(Qt::PointingHandCursor);

  QString capturedFilename = filename;
  connect(deleteBtn, &QPushButton::clicked, this, [this, capturedFilename]() {
    if (!m_registry) {
      return;
    }

    // Styled confirmation dialog
    auto *dlg = UIHelpers::createStyledDialog(this, 380);
    auto *ol = new QVBoxLayout(dlg);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyPopupBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    ly->setSpacing(12);

    auto *titleLabel = new QLabel("Delete Pack");
    UIHelpers::applyGoldTitleRole(titleLabel);
    titleLabel->setAlignment(Qt::AlignCenter);
    ly->addWidget(titleLabel);

    auto *msgLabel =
        new QLabel(QString("Delete \"%1\"?").arg(capturedFilename));
    UIHelpers::applyPopupLabelRole(msgLabel);
    msgLabel->setAlignment(Qt::AlignCenter);
    msgLabel->setWordWrap(true);
    ly->addWidget(msgLabel);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);
    auto *cancelBtn = new QPushButton("Cancel");
    UIHelpers::applyNeutralStyle(cancelBtn);
    cancelBtn->setMinimumHeight(36);
    auto *confirmBtn = new QPushButton("Delete");
    UIHelpers::applyCancelStyle(confirmBtn);
    confirmBtn->setMinimumHeight(36);
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(confirmBtn);
    ly->addLayout(btnLayout);

    connect(confirmBtn, &QPushButton::clicked, dlg,
            [this, dlg, capturedFilename]() {
              QString filePath =
                  QDir(m_registry->packsPath()).filePath(capturedFilename);
              if (QFile::remove(filePath)) {
                qInfo() << "MarkerPackBrowser: Deleted user pack"
                        << capturedFilename;
              }
              dlg->accept();
            });
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    UIHelpers::centerDialog(dlg);
    dlg->exec();
    dlg->deleteLater();

    // Refresh after dialog
    if (m_markerController) {
      m_markerController->reloadPacks();
    }
    rebuildCards();
    refreshTree();
  });

  btnRow->addWidget(deleteBtn);
  layout->addLayout(btnRow);

  return card;
}

// ============================================================================
// Tree View (existing functionality)
// ============================================================================

void MarkerPackBrowser::refreshTree() {
  m_tree->clear();

  if (!m_markerController) {
    return;
  }

  MarkerManager *mgr = m_markerController->manager();
  if (!mgr) {
    return;
  }

  const auto &packs = mgr->packs();
  if (packs.isEmpty()) {
    m_statusLabel->setText("No packs loaded");
    return;
  }

  for (const MarkerPack &pack : packs) {
    auto *packItem = new QTreeWidgetItem(m_tree);

    QString displayName =
        pack.name.isEmpty() ? QFileInfo(pack.path).baseName() : pack.name;
    if (!pack.author.isEmpty()) {
      displayName += QString::fromUtf8(" \xe2\x80\x94 by ") + pack.author;
    }
    packItem->setText(0, displayName);

    QString counts = QString("%1 markers").arg(pack.markerCount());
    if (pack.trailCount() > 0) {
      counts += QString(", %1 trails").arg(pack.trailCount());
    }
    if (pack.markerCount() == 0 && pack.trailCount() == 0 &&
        !pack.categories.isEmpty()) {
      counts += " (disabled)";
    }
    packItem->setText(1, counts);

    QStringList tipParts;
    if (!pack.description.isEmpty()) {
      tipParts << pack.description;
    }
    if (!pack.version.isEmpty()) {
      tipParts << QString("Version: %1").arg(pack.version);
    }
    if (!pack.website.isEmpty()) {
      tipParts << pack.website;
    }
    if (!tipParts.isEmpty()) {
      packItem->setToolTip(0, tipParts.join("\n"));
    }

    // Default to collapsed — user can expand as needed
    packItem->setExpanded(false);
    addCategoryItems(packItem, pack.categories);
  }

  int totalMarkers = std::accumulate(
      packs.begin(), packs.end(), 0,
      [](int sum, const MarkerPack &p) { return sum + p.markerCount(); });
  int totalTrails = std::accumulate(
      packs.begin(), packs.end(), 0,
      [](int sum, const MarkerPack &p) { return sum + p.trailCount(); });

  QString statusText =
      QString("%1 packs, %2 markers").arg(packs.size()).arg(totalMarkers);
  if (totalTrails > 0) {
    statusText += QString(", %1 trails").arg(totalTrails);
  }

  // Count disabled (metadata-only) packs
  int disabledCount = 0;
  for (const MarkerPack &p : packs) {
    if (p.markerCount() == 0 && p.trailCount() == 0 &&
        !p.categories.isEmpty()) {
      ++disabledCount;
    }
  }
  if (disabledCount > 0) {
    statusText += QString("\nDisabled packs show 0 markers "
                          "\xe2\x80\x94 enable in a profile to load full data");
  }

  m_statusLabel->setText(statusText);
}

void MarkerPackBrowser::addCategoryItems(
    QTreeWidgetItem *parent, const QList<MarkerCategory> &categories) {
  ImageCache *cache =
      m_markerController ? m_markerController->imageCache() : nullptr;

  for (const MarkerCategory &cat : categories) {
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, cat.displayName.isEmpty() ? cat.name : cat.displayName);

    if (cache && !cat.iconPath.isEmpty()) {
      QPixmap pix = cache->getPixmap(cat.iconPath);
      if (!pix.isNull()) {
        item->setIcon(0, QIcon(pix.scaled(16, 16, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation)));
      }
    }

    if (!cat.children.isEmpty()) {
      addCategoryItems(item, cat.children);
    }
  }
}

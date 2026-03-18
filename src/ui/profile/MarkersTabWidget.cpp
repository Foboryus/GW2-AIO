/**
 * @file MarkersTabWidget.cpp
 * @brief Markers tab for ProfileEditor — accordion pack cards + settings
 *
 * Packs page: MarkerPackCard accordion cards in a scroll area with search.
 * Settings page: MarkerSettingsWidget for overlay display settings.
 * Auto-saves via MarkerSettingsManager (debounced 2s).
 */

#include "MarkersTabWidget.h"

#include "core/ThemeManager.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerController.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerModels.h"
#include "features/markers/MarkerSettingsManager.h"
#include "ui/ProfileEditor.h" // For AccountProfile
#include "ui/ToggleSwitch.h"
#include "ui/UIHelpers.h"
#include "ui/profile/MarkerPackCard.h"
#include "ui/profile/MarkerSettingsWidget.h"


#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

MarkersTabWidget::MarkersTabWidget(AccountProfile &profile,
                                   MarkerSettingsManager *markerSettings,
                                   MarkerController *markerController,
                                   QWidget *parent)
    : QWidget(parent), m_profile(profile), m_markerSettings(markerSettings),
      m_markerController(markerController) {
  setupUI();
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void MarkersTabWidget::load() {
  if (!m_markerSettings || m_profile.id.isEmpty()) {
    return;
  }

  m_markerSettings->loadForProfile(m_profile.id);

  // Live sync: when overlay changes settings, update our cards
  connect(m_markerSettings, &MarkerSettingsManager::settingsChanged, this,
          &MarkersTabWidget::syncTogglesFromSettings, Qt::UniqueConnection);

  // Sync settings sub-tab with loaded profile data
  if (m_settingsWidget) {
    m_settingsWidget->syncFromSettings();
  }

  // Check if packs are already loaded
  if (m_markerController && m_markerController->manager() &&
      !m_markerController->manager()->packs().isEmpty()) {
    m_packsReady = true;
    populateCards();
  } else {
    // Packs not ready yet — show loading state
    m_packsReady = false;
    if (m_loadingWidget) {
      m_loadingWidget->show();
    }
    if (m_scrollArea) {
      m_scrollArea->hide();
    }
    if (m_selectAllBtn) {
      m_selectAllBtn->setEnabled(false);
    }
    if (m_clearAllBtn) {
      m_clearAllBtn->setEnabled(false);
    }

    // Connect to packsLoaded signal for when packs become available
    if (m_markerController && m_markerController->manager()) {
      connect(m_markerController->manager(), &MarkerManager::packsLoaded, this,
              &MarkersTabWidget::onPacksLoaded, Qt::UniqueConnection);
      connect(m_markerController->manager(), &MarkerManager::packsLoadIssues,
              this, &MarkersTabWidget::onPacksLoadIssues, Qt::UniqueConnection);
    }
  }
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void MarkersTabWidget::onPacksLoaded() {
  m_packsReady = true;
  populateCards();
}

void MarkersTabWidget::onPacksLoadIssues(const QStringList &missingPacks,
                                         const QStringList &failedPacks) {
  if (!m_warningBanner) {
    return;
  }

  const auto &theme = ThemeManager::instance().activeTheme();

  QStringList lines;

  if (!failedPacks.isEmpty()) {
    lines << QString("<span style='color:%1;'>"
                     "<b>%2 pack(s) failed to parse:</b> %3</span>")
                 .arg(theme.colors.warning)
                 .arg(failedPacks.size())
                 .arg(failedPacks.join(", "));
  }

  if (!missingPacks.isEmpty()) {
    lines << QString("<span style='color:%1;'>"
                     "%2 pack(s) not installed: %3</span>")
                 .arg(theme.colors.textHint)
                 .arg(missingPacks.size())
                 .arg(missingPacks.join(", "));
  }

  if (!lines.isEmpty()) {
    m_warningBanner->setText(lines.join("<br>"));
    m_warningBanner->show();
  } else {
    m_warningBanner->hide();
  }
}

void MarkersTabWidget::onSelectAllClicked() {
  if (!m_markerSettings || !m_markerController ||
      !m_markerController->manager()) {
    return;
  }

  const auto &packs = m_markerController->manager()->packs();
  QStringList packIds;
  for (const auto &pack : packs) {
    packIds.append(pack.id);
  }

  // Batch update settings (single save + single signal)
  m_suppressSettingsSync = true;
  m_markerSettings->setAllPacksEnabled(packIds, true);
  m_suppressSettingsSync = false;

  // Update card toggles
  for (auto *card : m_packCards) {
    card->syncFromSettings();
  }
}

void MarkersTabWidget::onClearAllClicked() {
  if (!m_markerSettings || !m_markerController ||
      !m_markerController->manager()) {
    return;
  }

  const auto &packs = m_markerController->manager()->packs();
  QStringList packIds;
  for (const auto &pack : packs) {
    packIds.append(pack.id);
  }

  // Batch update settings (single save + single signal)
  m_suppressSettingsSync = true;
  m_markerSettings->setAllPacksEnabled(packIds, false);
  m_suppressSettingsSync = false;

  // Update card toggles
  for (auto *card : m_packCards) {
    card->syncFromSettings();
  }
}

void MarkersTabWidget::onSearchTextChanged(const QString &text) {
  for (auto *card : m_packCards) {
    card->applyFilter(text);
  }
}

// ---------------------------------------------------------------------------
// Private — UI setup
// ---------------------------------------------------------------------------

void MarkersTabWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(8, 8, 8, 8);
  mainLayout->setSpacing(8);

  // =========================================================================
  // Sub-tab bar: Packs | Settings
  // =========================================================================
  auto *tabBar = new QHBoxLayout();
  tabBar->setSpacing(4);

  m_packsTabBtn = new QPushButton("Packs");
  m_packsTabBtn->setCheckable(true);
  m_packsTabBtn->setChecked(true);
  m_packsTabBtn->setMinimumHeight(32);
  m_packsTabBtn->setCursor(Qt::PointingHandCursor);
  connect(m_packsTabBtn, &QPushButton::clicked, this,
          [this]() { switchToTab(0); });
  tabBar->addWidget(m_packsTabBtn);

  m_settingsTabBtn = new QPushButton("Settings");
  m_settingsTabBtn->setCheckable(true);
  m_settingsTabBtn->setChecked(false);
  m_settingsTabBtn->setMinimumHeight(32);
  m_settingsTabBtn->setCursor(Qt::PointingHandCursor);
  connect(m_settingsTabBtn, &QPushButton::clicked, this,
          [this]() { switchToTab(1); });
  tabBar->addWidget(m_settingsTabBtn);

  tabBar->addStretch();
  mainLayout->addLayout(tabBar);

  // =========================================================================
  // Stacked widget: page 0 = Packs, page 1 = Settings
  // =========================================================================
  m_stack = new QStackedWidget();

  // --- Page 0: Packs ---
  auto *packsPage = new QWidget();
  auto *packsLayout = new QVBoxLayout(packsPage);
  packsLayout->setContentsMargins(0, 0, 0, 0);
  packsLayout->setSpacing(8);

  // Search bar
  m_searchBar = new QLineEdit();
  m_searchBar->setPlaceholderText("Search packs and categories...");
  UIHelpers::applyRole(m_searchBar, "input");
  m_searchBar->setMinimumHeight(28);
  connect(m_searchBar, &QLineEdit::textChanged, this,
          &MarkersTabWidget::onSearchTextChanged);
  packsLayout->addWidget(m_searchBar);

  // Action bar: Enable All / Disable All pack buttons
  auto *actionBar = new QHBoxLayout();
  actionBar->setSpacing(8);

  m_selectAllBtn = new QPushButton("Enable All Packs");
  UIHelpers::applyNeutralStyle(m_selectAllBtn);
  m_selectAllBtn->setMinimumWidth(90);
  connect(m_selectAllBtn, &QPushButton::clicked, this,
          &MarkersTabWidget::onSelectAllClicked);
  actionBar->addWidget(m_selectAllBtn);

  m_clearAllBtn = new QPushButton("Disable All Packs");
  UIHelpers::applyNeutralStyle(m_clearAllBtn);
  m_clearAllBtn->setMinimumWidth(90);
  connect(m_clearAllBtn, &QPushButton::clicked, this,
          &MarkersTabWidget::onClearAllClicked);
  actionBar->addWidget(m_clearAllBtn);

  actionBar->addStretch();

  m_statusLabel = new QLabel();
  UIHelpers::applyHintRole(m_statusLabel);
  actionBar->addWidget(m_statusLabel);

  packsLayout->addLayout(actionBar);

  // Loading placeholder
  m_loadingWidget = new QWidget();
  auto *loadingLayout = new QVBoxLayout(m_loadingWidget);
  loadingLayout->setAlignment(Qt::AlignCenter);
  auto *loadingLabel = new QLabel("Loading packs...");
  UIHelpers::applyHintRole(loadingLabel);
  loadingLabel->setAlignment(Qt::AlignCenter);
  loadingLayout->addWidget(loadingLabel);
  packsLayout->addWidget(m_loadingWidget);
  m_loadingWidget->hide();

  // Warning banner for missing/failed packs
  m_warningBanner = new QLabel();
  m_warningBanner->setWordWrap(true);
  m_warningBanner->setTextFormat(Qt::RichText);
  UIHelpers::applyInfoBannerRole(m_warningBanner);
  m_warningBanner->hide();
  packsLayout->addWidget(m_warningBanner);

  // Scroll area for pack cards
  m_scrollArea = new QScrollArea();
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setFrameShape(QFrame::NoFrame);

  auto *scrollContent = new QWidget();
  m_cardsLayout = new QVBoxLayout(scrollContent);
  m_cardsLayout->setContentsMargins(0, 0, 0, 0);
  m_cardsLayout->setSpacing(4);
  m_cardsLayout->addStretch(); // Push cards to top

  m_scrollArea->setWidget(scrollContent);
  packsLayout->addWidget(m_scrollArea);

  m_stack->addWidget(packsPage);

  // --- Page 1: Settings (in scroll area to prevent vertical compression) ---
  m_settingsWidget = new MarkerSettingsWidget(m_markerSettings, this);
  connect(m_settingsWidget, &MarkerSettingsWidget::modified, this,
          &MarkersTabWidget::modified);

  auto *settingsScroll = new QScrollArea();
  settingsScroll->setWidgetResizable(true);
  settingsScroll->setFrameShape(QFrame::NoFrame);
  settingsScroll->setWidget(m_settingsWidget);
  m_stack->addWidget(settingsScroll);

  m_stack->setCurrentIndex(0);
  mainLayout->addWidget(m_stack);

  // Apply initial tab styling
  switchToTab(0);
}

// ---------------------------------------------------------------------------
// Sub-tab switching
// ---------------------------------------------------------------------------

void MarkersTabWidget::switchToTab(int index) {
  m_stack->setCurrentIndex(index);
  m_packsTabBtn->setChecked(index == 0);
  m_settingsTabBtn->setChecked(index == 1);

  // Role-based sub-tab styling (themed via 08_Containers.h)
  UIHelpers::applyRole(m_packsTabBtn,
                       index == 0 ? "subTabActive" : "subTabInactive");
  UIHelpers::applyRole(m_settingsTabBtn,
                       index == 1 ? "subTabActive" : "subTabInactive");
}

// ---------------------------------------------------------------------------
// Card population
// ---------------------------------------------------------------------------

void MarkersTabWidget::populateCards() {
  if (!m_markerSettings || !m_markerController ||
      !m_markerController->manager() || !m_cardsLayout) {
    return;
  }

  // Show scroll area, hide loading
  if (m_scrollArea) {
    m_scrollArea->show();
  }
  if (m_loadingWidget) {
    m_loadingWidget->hide();
  }
  if (m_selectAllBtn) {
    m_selectAllBtn->setEnabled(true);
  }
  if (m_clearAllBtn) {
    m_clearAllBtn->setEnabled(true);
  }

  // Clear old cards
  for (auto *card : m_packCards) {
    m_cardsLayout->removeWidget(card);
    card->deleteLater();
  }
  m_packCards.clear();

  const auto &packs = m_markerController->manager()->packs();
  ImageCache *cache =
      m_markerController ? m_markerController->imageCache() : nullptr;

  int packCount = 0;
  int categoryCount = 0;

  for (const auto &pack : packs) {
    // Get pack icon from first category (if available)
    QPixmap packIcon;
    if (cache && !pack.categories.isEmpty() &&
        !pack.categories.first().iconPath.isEmpty()) {
      packIcon = cache->getPixmap(pack.categories.first().iconPath);
    }

    // Build set of category paths that have markers/trails.
    // Include all ancestor prefixes so parent categories get hasContent=true
    // when any descendant has content. (Same logic as OverlayMenuWidget.)
    // For disabled (metadata-only) packs, markers/trails are empty →
    // contentPaths is empty → all checkboxes shown (user pre-config).
    QSet<QString> contentPaths;
    auto addPathAndAncestors = [&contentPaths](const QString &type) {
      if (type.isEmpty())
        return;
      QStringList parts = type.split('.');
      QString path;
      for (const QString &part : parts) {
        path = path.isEmpty() ? part : path + "." + part;
        contentPaths.insert(path);
      }
    };
    for (const Marker &m : pack.markers) {
      addPathAndAncestors(m.type);
    }
    for (const Trail &t : pack.trails) {
      addPathAndAncestors(t.type);
    }

    auto *card = new MarkerPackCard(pack.id, pack.name, packIcon,
                                    pack.categories, contentPaths,
                                    m_markerSettings);

    // Wire card signals to MarkerController for runtime visibility
    // NOTE: Pack toggle does NOT iterate categories — isCategoryVisible()
    // checks isPackEnabled() directly, so setPackEnabled() is sufficient.
    connect(card, &MarkerPackCard::packToggled, this,
            [this](const QString & /*packId*/, bool /*enabled*/) {
              emit modified();
            });

    connect(card, &MarkerPackCard::categoryToggled, this,
            [this](const QString & /*packId*/, const QString &catPath,
                   bool enabled) {
              if (m_markerController && m_markerController->manager()) {
                m_markerController->manager()->updateCategoryVisibility(
                    catPath, enabled);
              }
              emit modified();
            });

    // Insert before the stretch at end
    m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
    m_packCards.append(card);

    // Count categories recursively
    std::function<int(const QList<MarkerCategory> &)> countCats;
    countCats = [&](const QList<MarkerCategory> &cats) -> int {
      int count = 0;
      for (const auto &c : cats) {
        if (!c.isSeparator)
          count++;
        count += countCats(c.children);
      }
      return count;
    };
    categoryCount += countCats(pack.categories);
    packCount++;
  }

  // Update status label
  if (m_statusLabel) {
    m_statusLabel->setText(
        QString("%1 packs, %2 categories").arg(packCount).arg(categoryCount));
  }
}

// ---------------------------------------------------------------------------
// Live sync: update cards when overlay changes settings
// ---------------------------------------------------------------------------

void MarkersTabWidget::syncTogglesFromSettings() {
  if (m_suppressSettingsSync || !m_markerSettings) {
    return;
  }

  // Sync all pack cards
  for (auto *card : m_packCards) {
    card->syncFromSettings();
  }

  // Also sync the settings sub-tab widgets
  if (m_settingsWidget) {
    m_settingsWidget->syncFromSettings();
  }
}

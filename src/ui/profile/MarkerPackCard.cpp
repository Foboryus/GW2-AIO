/**
 * @file MarkerPackCard.cpp
 * @brief Accordion-style pack card — collapsed header + expandable categories
 *
 * Lazy-loads category checkboxes on first expand.
 * All styling via UIHelpers roles (no inline styles).
 */

#include "MarkerPackCard.h"

#include "features/markers/MarkerModels.h"
#include "features/markers/MarkerSettingsManager.h"
#include "ui/ToggleSwitch.h"
#include "ui/UIHelpers.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>

MarkerPackCard::MarkerPackCard(const QString &packId, const QString &packName,
                               const QPixmap &packIcon,
                               const QList<MarkerCategory> &categories,
                               const QSet<QString> &contentPaths,
                               MarkerSettingsManager *settings, QWidget *parent)
    : QWidget(parent), m_packId(packId), m_packName(packName),
      m_packIcon(packIcon), m_categories(categories),
      m_contentPaths(contentPaths), m_settings(settings) {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  setupHeader();

  // Body container (hidden by default, lazy-populated)
  m_body = new QWidget();
  m_body->setVisible(false);
  auto *bodyLayout = new QVBoxLayout(m_body);
  bodyLayout->setContentsMargins(12, 4, 12, 8);
  bodyLayout->setSpacing(4);
  mainLayout->addWidget(m_body);

  // Apply card styling via role
  UIHelpers::applyRole(this, "markerPackCard");

  updateBadge();
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

void MarkerPackCard::setupHeader() {
  auto *header = new QWidget();
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(8, 6, 8, 6);
  headerLayout->setSpacing(8);

  // Expand arrow (SVG chevron — themed via ThemeManager)
  m_expandArrow = new QLabel();
  m_expandArrow->setFixedSize(16, 16);
  UIHelpers::setThemedPixmap(m_expandArrow, "chevron-right", 16);
  headerLayout->addWidget(m_expandArrow);

  // Pack icon
  m_iconLabel = new QLabel();
  if (!m_packIcon.isNull()) {
    m_iconLabel->setPixmap(m_packIcon.scaled(24, 24, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
  }
  m_iconLabel->setFixedSize(24, 24);
  headerLayout->addWidget(m_iconLabel);

  // Pack name
  m_nameLabel = new QLabel(m_packName);
  UIHelpers::applyRole(m_nameLabel, "cardTitle");
  headerLayout->addWidget(m_nameLabel, 1);

  // Badge (enabled count indicator)
  m_badgeLabel = new QLabel();
  m_badgeLabel->setFixedSize(20, 20);
  UIHelpers::applyHintRole(m_badgeLabel);
  headerLayout->addWidget(m_badgeLabel);

  // Pack toggle
  m_packToggle = new ToggleSwitch();
  bool packEnabled = m_settings ? m_settings->isPackEnabled(m_packId) : true;
  m_packToggle->setChecked(packEnabled);
  connect(m_packToggle, &ToggleSwitch::toggled, this, [this](bool checked) {
    if (m_suppressSync)
      return;
    m_suppressSync = true;
    if (m_settings) {
      m_settings->setPackEnabled(m_packId, checked);
    }
    updateBadge();
    m_suppressSync = false;
    emit packToggled(m_packId, checked);
  });
  headerLayout->addWidget(m_packToggle);

  layout()->addWidget(header);
}

// ---------------------------------------------------------------------------
// Expand / collapse
// ---------------------------------------------------------------------------

void MarkerPackCard::mousePressEvent(QMouseEvent *event) {
  // Only respond to clicks on the header area (top ~40px)
  if (event->position().y() < 40) {
    toggleExpand();
  }
  QWidget::mousePressEvent(event);
}

bool MarkerPackCard::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::MouseButtonPress) {
    auto *arrow = qobject_cast<QLabel *>(watched);
    if (arrow && arrow->property("isExpander").toBool()) {
      auto *container = qobject_cast<QWidget *>(
          arrow->property("childContainer").value<QObject *>());
      if (container) {
        bool visible = !container->isVisible();
        container->setVisible(visible);
        UIHelpers::setThemedPixmap(arrow, visible ? "chevron-down" : "chevron-right", 12);
      }
      return true; // consumed
    }
  }
  return QWidget::eventFilter(watched, event);
}

void MarkerPackCard::toggleExpand() {
  m_expanded = !m_expanded;

  // Lazy-build categories on first expand
  if (m_expanded && !m_categoriesBuilt) {
    buildCategories();
  }

  m_body->setVisible(m_expanded);
  UIHelpers::setThemedPixmap(m_expandArrow, m_expanded ? "chevron-down" : "chevron-right", 16);
}

// ---------------------------------------------------------------------------
// Lazy category building
// ---------------------------------------------------------------------------

void MarkerPackCard::buildCategories() {
  if (m_categoriesBuilt)
    return;
  m_categoriesBuilt = true;

  auto *bodyLayout = qobject_cast<QVBoxLayout *>(m_body->layout());
  if (!bodyLayout)
    return;

  // Empty metadata-only pack: show hint instead of useless buttons
  if (m_categories.isEmpty()) {
    auto *hint = new QLabel("Enable this pack in a profile to see categories");
    UIHelpers::applyHintRole(hint);
    hint->setWordWrap(true);
    hint->setContentsMargins(16, 8, 16, 8);
    bodyLayout->addWidget(hint);
    return;
  }

  // Enable All / Disable All icon buttons
  auto *btnBar = new QHBoxLayout();
  btnBar->setSpacing(4);

  auto *enableAllBtn = new QPushButton();
  UIHelpers::setThemedIcon(enableAllBtn, "check-green");
  enableAllBtn->setIconSize(QSize(16, 16));
  enableAllBtn->setFixedSize(28, 28);
  enableAllBtn->setToolTip("Enable all categories");
  UIHelpers::applyNeutralStyle(enableAllBtn);
  connect(enableAllBtn, &QPushButton::clicked, this, [this]() {
    qInfo() << "MarkerPackCard: Enable all" << m_categoryCheckboxes.size()
            << "categories for pack" << m_packId;
    m_suppressSync = true;
    QStringList paths;
    paths.reserve(m_categoryCheckboxes.size());
    for (auto it = m_categoryCheckboxes.cbegin();
         it != m_categoryCheckboxes.cend(); ++it) {
      it.value()->blockSignals(true);
      it.value()->setChecked(true);
      it.value()->blockSignals(false);
      paths.append(it.key());
    }
    if (m_settings) {
      m_settings->setAllCategoriesEnabled(m_packId, paths, true);
    }
    updateBadge();
    m_suppressSync = false;
    qInfo() << "MarkerPackCard: Enable all complete for" << m_packId;
  });
  btnBar->addWidget(enableAllBtn);

  auto *disableAllBtn = new QPushButton();
  UIHelpers::setThemedIcon(disableAllBtn, "x-circle");
  disableAllBtn->setIconSize(QSize(16, 16));
  disableAllBtn->setFixedSize(28, 28);
  disableAllBtn->setToolTip("Disable all categories");
  UIHelpers::applyNeutralStyle(disableAllBtn);
  connect(disableAllBtn, &QPushButton::clicked, this, [this]() {
    qInfo() << "MarkerPackCard: Disable all" << m_categoryCheckboxes.size()
            << "categories for pack" << m_packId;
    m_suppressSync = true;
    QStringList paths;
    paths.reserve(m_categoryCheckboxes.size());
    for (auto it = m_categoryCheckboxes.cbegin();
         it != m_categoryCheckboxes.cend(); ++it) {
      it.value()->blockSignals(true);
      it.value()->setChecked(false);
      it.value()->blockSignals(false);
      paths.append(it.key());
    }
    if (m_settings) {
      m_settings->setAllCategoriesEnabled(m_packId, paths, false);
    }
    updateBadge();
    m_suppressSync = false;
    qInfo() << "MarkerPackCard: Disable all complete for" << m_packId;
  });
  btnBar->addWidget(disableAllBtn);
  btnBar->addStretch();
  bodyLayout->addLayout(btnBar);

  // Add category checkboxes recursively
  addCategoryCheckboxes(bodyLayout, m_categories, 0);
}

void MarkerPackCard::addCategoryCheckboxes(
    QVBoxLayout *layout, const QList<MarkerCategory> &categories, int depth) {
  for (const auto &cat : categories) {
    QString label = cat.displayName.isEmpty() ? cat.name : cat.displayName;

    // Separator categories: bold non-togglable title
    if (cat.isSeparator) {
      auto *sepRow = new QHBoxLayout();
      sepRow->setContentsMargins(depth * 16, 4, 0, 2);
      auto *sepLabel = new QLabel(label);
      QFont boldFont = sepLabel->font();
      boldFont.setBold(true);
      sepLabel->setFont(boldFont);
      UIHelpers::applyHintRole(sepLabel);
      sepRow->addWidget(sepLabel);
      sepRow->addStretch();
      layout->addLayout(sepRow);

      // Still recurse into children (they may be toggleable)
      if (!cat.children.isEmpty()) {
        addCategoryCheckboxes(layout, cat.children, depth + 1);
      }
      continue;
    }

    // Content-less categories: dimmed label, no checkbox
    // Only filter when contentPaths is non-empty (enabled packs have content
    // data; disabled/metadata-only packs have empty contentPaths → show all)
    if (!m_contentPaths.isEmpty() && !m_contentPaths.contains(cat.fullName)) {
      // Content-less WITH children: collapsible section (chevron + dimmed label)
      if (!cat.children.isEmpty()) {
        auto *sectionRow = new QHBoxLayout();
        sectionRow->setContentsMargins(depth * 16, 0, 0, 0);

        auto *arrow = new QLabel();
        arrow->setFixedSize(12, 12);
        UIHelpers::setThemedPixmap(arrow, "chevron-right", 12);
        sectionRow->addWidget(arrow);

        auto *dimLabel = new QLabel(label);
        UIHelpers::applyHintRole(dimLabel);
        dimLabel->setEnabled(false);
        sectionRow->addWidget(dimLabel, 1);
        layout->addLayout(sectionRow);

        // Collapsible children container (collapsed by default)
        auto *childContainer = new QWidget();
        childContainer->setVisible(false);
        auto *childLayout = new QVBoxLayout(childContainer);
        childLayout->setContentsMargins(0, 0, 0, 0);
        childLayout->setSpacing(2);

        addCategoryCheckboxes(childLayout, cat.children, depth + 1);
        layout->addWidget(childContainer);

        // Wire arrow click via event filter
        arrow->setCursor(Qt::PointingHandCursor);
        arrow->installEventFilter(this);
        arrow->setProperty(
            "childContainer",
            QVariant::fromValue(static_cast<QObject *>(childContainer)));
        arrow->setProperty("isExpander", true);
      } else {
        // Content-less LEAF: flat dimmed label
        auto *dimRow = new QHBoxLayout();
        dimRow->setContentsMargins(depth * 16, 0, 0, 0);
        auto *dimLabel = new QLabel(label);
        UIHelpers::applyHintRole(dimLabel);
        dimLabel->setEnabled(false);
        dimRow->addWidget(dimLabel);
        dimRow->addStretch();
        layout->addLayout(dimRow);
      }
      continue;
    }

    // Category with children: collapsible section
    if (!cat.children.isEmpty()) {
      // Create a clickable row: arrow + checkbox
      auto *sectionRow = new QHBoxLayout();
      sectionRow->setContentsMargins(depth * 16, 0, 0, 0);

      auto *arrow = new QLabel();
      arrow->setFixedSize(12, 12);
      UIHelpers::setThemedPixmap(arrow, "chevron-right", 12);
      sectionRow->addWidget(arrow);

      auto *cb = new QCheckBox(label);
      bool enabled =
          m_settings
              ? m_settings->isCategoryDirectEnabled(m_packId, cat.fullName)
              : true;
      cb->setChecked(enabled);
      connect(cb, &QCheckBox::toggled, this,
              [this, catPath = cat.fullName](bool checked) {
                if (m_suppressSync)
                  return;
                m_suppressSync = true;
                if (m_settings) {
                  m_settings->setCategoryEnabled(m_packId, catPath, checked);
                  // Auto-enable pack when a child turns ON
                  if (checked && !m_settings->isPackEnabled(m_packId)) {
                    m_settings->setPackEnabled(m_packId, true);
                    m_packToggle->blockSignals(true);
                    m_packToggle->setChecked(true);
                    m_packToggle->blockSignals(false);
                    emit packToggled(m_packId, true);
                  }
                }
                updateBadge();
                m_suppressSync = false;
                emit categoryToggled(m_packId, catPath, checked);
              });
      m_categoryCheckboxes.insert(cat.fullName, cb);
      sectionRow->addWidget(cb, 1);
      layout->addLayout(sectionRow);

      // Collapsible children container
      auto *childContainer = new QWidget();
      childContainer->setVisible(false); // Collapsed by default
      auto *childLayout = new QVBoxLayout(childContainer);
      childLayout->setContentsMargins(0, 0, 0, 0);
      childLayout->setSpacing(2);

      addCategoryCheckboxes(childLayout, cat.children, depth + 1);
      layout->addWidget(childContainer);

      // Click arrow to expand/collapse children
      arrow->setCursor(Qt::PointingHandCursor);
      arrow->installEventFilter(this);
      // Store expand state via dynamic properties
      arrow->setProperty(
          "childContainer",
          QVariant::fromValue(static_cast<QObject *>(childContainer)));
      connect(arrow, &QObject::destroyed, this, []() {}); // prevent dangling
      // Use mouse press on arrow label via event filter (handled below)
      arrow->setProperty("isExpander", true);

    } else {
      // Leaf category: simple checkbox
      auto *cb = new QCheckBox(label);
      bool enabled =
          m_settings
              ? m_settings->isCategoryDirectEnabled(m_packId, cat.fullName)
              : true;
      cb->setChecked(enabled);
      connect(cb, &QCheckBox::toggled, this,
              [this, catPath = cat.fullName](bool checked) {
                if (m_suppressSync)
                  return;
                m_suppressSync = true;
                if (m_settings) {
                  m_settings->setCategoryEnabled(m_packId, catPath, checked);
                  // Auto-enable pack when a child turns ON
                  if (checked && !m_settings->isPackEnabled(m_packId)) {
                    m_settings->setPackEnabled(m_packId, true);
                    m_packToggle->blockSignals(true);
                    m_packToggle->setChecked(true);
                    m_packToggle->blockSignals(false);
                    emit packToggled(m_packId, true);
                  }
                }
                updateBadge();
                m_suppressSync = false;
                emit categoryToggled(m_packId, catPath, checked);
              });
      m_categoryCheckboxes.insert(cat.fullName, cb);

      if (depth > 0) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(depth * 16, 0, 0, 0);
        row->addWidget(cb);
        layout->addLayout(row);
      } else {
        layout->addWidget(cb);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Badge update
// ---------------------------------------------------------------------------

void MarkerPackCard::updateBadge() {
  if (!m_settings)
    return;

  // Count enabled categories
  int total = m_categoryCheckboxes.size();
  int enabled = 0;
  for (auto it = m_categoryCheckboxes.cbegin();
       it != m_categoryCheckboxes.cend(); ++it) {
    if (it.value()->isChecked()) {
      ++enabled;
    }
  }

  // If categories not built yet, count from settings
  if (!m_categoriesBuilt) {
    total = 0;
    enabled = 0;
    // We can't count accurately without building, so just check pack state
    bool packOn = m_settings->isPackEnabled(m_packId);
    m_badgeLabel->setText(packOn ? "●" : "");
    m_badgeLabel->setToolTip(packOn ? "Pack enabled" : "Pack disabled");
    return;
  }

  if (enabled > 0) {
    m_badgeLabel->setText("●");
    m_badgeLabel->setToolTip(
        QString("%1/%2 categories enabled").arg(enabled).arg(total));
  } else {
    m_badgeLabel->setText("");
    m_badgeLabel->setToolTip("No categories enabled");
  }
}

// ---------------------------------------------------------------------------
// Sync from MarkerSettingsManager (bidirectional)
// ---------------------------------------------------------------------------

void MarkerPackCard::syncFromSettings() {
  if (!m_settings)
    return;
  m_suppressSync = true;

  // Sync pack toggle
  bool packEnabled = m_settings->isPackEnabled(m_packId);
  if (m_packToggle->isChecked() != packEnabled) {
    m_packToggle->blockSignals(true);
    m_packToggle->setChecked(packEnabled);
    m_packToggle->blockSignals(false);
  }

  // Sync category checkboxes (only if built)
  if (m_categoriesBuilt) {
    for (auto it = m_categoryCheckboxes.cbegin();
         it != m_categoryCheckboxes.cend(); ++it) {
      bool catEnabled = m_settings->isCategoryDirectEnabled(m_packId, it.key());
      if (it.value()->isChecked() != catEnabled) {
        it.value()->blockSignals(true);
        it.value()->setChecked(catEnabled);
        it.value()->blockSignals(false);
      }
    }
  }

  updateBadge();
  m_suppressSync = false;
}

// ---------------------------------------------------------------------------
// Search filter
// ---------------------------------------------------------------------------

bool MarkerPackCard::applyFilter(const QString &filter) {
  if (filter.isEmpty()) {
    setVisible(true);
    return true;
  }

  // Check pack name
  if (m_packName.contains(filter, Qt::CaseInsensitive)) {
    setVisible(true);
    return true;
  }

  // Check category names (search through stored categories, not just built
  // checkboxes)
  std::function<bool(const QList<MarkerCategory> &)> searchCategories;
  searchCategories = [&](const QList<MarkerCategory> &cats) -> bool {
    for (const auto &cat : cats) {
      QString name = cat.displayName.isEmpty() ? cat.name : cat.displayName;
      if (name.contains(filter, Qt::CaseInsensitive)) {
        return true;
      }
      if (!cat.children.isEmpty() && searchCategories(cat.children)) {
        return true;
      }
    }
    return false;
  };

  bool matches = searchCategories(m_categories);
  setVisible(matches);
  return matches;
}

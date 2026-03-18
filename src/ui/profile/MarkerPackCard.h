#pragma once

/**
 * @brief Accordion-style card widget for a single marker pack
 *
 * Collapsed: [Icon] Pack Name ── [badge] [ToggleSwitch]
 * Expanded: category checkboxes + Enable All / Disable All buttons
 *
 * Categories are lazy-loaded on first expand (avoids perf issue).
 * All visual elements are themable via UIHelpers roles.
 *
 * DO NOT ADD:
 * - Settings controls (belongs in MarkerSettingsWidget)
 * - Inline styles (use UIHelpers roles)
 */

#include <QSet>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QPropertyAnimation;
class ToggleSwitch;
class MarkerSettingsManager;

struct MarkerCategory;

class MarkerPackCard : public QWidget {
  Q_OBJECT

public:
  explicit MarkerPackCard(const QString &packId, const QString &packName,
                          const QPixmap &packIcon,
                          const QList<MarkerCategory> &categories,
                          const QSet<QString> &contentPaths,
                          MarkerSettingsManager *settings,
                          QWidget *parent = nullptr);

  QString packId() const { return m_packId; }

  /**
   * @brief Sync all toggle/checkbox states from MarkerSettingsManager
   * Called when overlay changes settings (bidirectional sync)
   */
  void syncFromSettings();

  /**
   * @brief Update visibility based on search filter
   * Matches against pack name and category names
   * @return true if this card matches the filter
   */
  bool applyFilter(const QString &filter);

signals:
  void packToggled(const QString &packId, bool enabled);
  void categoryToggled(const QString &packId, const QString &catPath,
                       bool enabled);

private:
  void setupHeader();
  void toggleExpand();
  void buildCategories(); // Lazy — only called on first expand
  void updateBadge();
  void addCategoryCheckboxes(QVBoxLayout *layout,
                             const QList<MarkerCategory> &categories,
                             int depth);

  void mousePressEvent(QMouseEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

  QString m_packId;
  QString m_packName;
  QPixmap m_packIcon;
  QList<MarkerCategory> m_categories;
  QSet<QString> m_contentPaths;
  MarkerSettingsManager *m_settings;

  // Header widgets
  QLabel *m_iconLabel = nullptr;
  QLabel *m_nameLabel = nullptr;
  QLabel *m_badgeLabel = nullptr;
  ToggleSwitch *m_packToggle = nullptr;
  QLabel *m_expandArrow = nullptr;

  // Body (expandable)
  QWidget *m_body = nullptr;
  bool m_expanded = false;
  bool m_categoriesBuilt = false;
  bool m_suppressSync = false;

  // Category checkboxes mapped by catPath for sync
  QHash<QString, QCheckBox *> m_categoryCheckboxes;
};

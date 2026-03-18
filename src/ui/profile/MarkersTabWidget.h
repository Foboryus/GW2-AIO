#pragma once

/**
 * @brief Markers tab for ProfileEditor
 *
 * Contains two sub-tabs: "Packs" (accordion pack cards) and "Settings"
 * (overlay display settings). Saves independently via MarkerSettingsManager.
 *
 * DO NOT ADD:
 * - Marker rendering logic (belongs in renderers)
 * - Pack loading logic (belongs in MarkerController/MarkerManager)
 * - Inline implementations
 */

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class MarkerController;
class MarkerPackCard;
class MarkerSettingsManager;
class MarkerSettingsWidget;
struct AccountProfile;

class MarkersTabWidget : public QWidget {
  Q_OBJECT

public:
  explicit MarkersTabWidget(AccountProfile &profile,
                            MarkerSettingsManager *markerSettings,
                            MarkerController *markerController,
                            QWidget *parent = nullptr);

  /**
   * @brief Load toggle states from MarkerSettingsManager for current profile
   */
  void load();

signals:
  void modified();

private slots:
  void onPacksLoaded();
  void onPacksLoadIssues(const QStringList &missingPacks,
                         const QStringList &failedPacks);
  void onSelectAllClicked();
  void onClearAllClicked();
  void onSearchTextChanged(const QString &text);
  void syncTogglesFromSettings();

private:
  void setupUI();
  void populateCards();
  void switchToTab(int index);

  AccountProfile &m_profile;
  MarkerSettingsManager *m_markerSettings;
  MarkerController *m_markerController;

  // --- Sub-tab switching ---
  QPushButton *m_packsTabBtn = nullptr;
  QPushButton *m_settingsTabBtn = nullptr;
  QStackedWidget *m_stack = nullptr;

  // --- Packs page ---
  QLineEdit *m_searchBar = nullptr;
  QLabel *m_statusLabel = nullptr;
  QPushButton *m_selectAllBtn = nullptr;
  QPushButton *m_clearAllBtn = nullptr;
  QWidget *m_loadingWidget = nullptr;
  QLabel *m_warningBanner = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QVBoxLayout *m_cardsLayout = nullptr;
  QList<MarkerPackCard *> m_packCards;

  // --- Settings page ---
  MarkerSettingsWidget *m_settingsWidget = nullptr;

  bool m_packsReady = false;
  bool m_suppressSettingsSync = false;
};

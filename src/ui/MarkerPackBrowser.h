#pragma once

/**
 * @brief Marker Pack Browser — two-column split panel for managing marker packs
 *
 * Layout:
 * - Page header with Auto-Update toggle + Check for Updates button
 * - Left panel:  Installed packs (cards with Update/Delete)
 * - Right panel: Available packs (cards with Download)
 * - Existing tree view of pack categories below
 *
 * All colors driven by ThemeData tokens (widgets.packCard*).
 *
 * DO NOT ADD:
 * - Inline styles (use ThemeManager tokens)
 * - Rendering logic (belongs in renderers)
 */

#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

class DataService;
class MarkerController;
class MarkerPackRegistry;
class LabeledToggle;
struct MarkerCategory;
struct OnlineMarkerPack;

class MarkerPackBrowser : public QWidget {
  Q_OBJECT

public:
  explicit MarkerPackBrowser(DataService *dataService,
                             MarkerController *markerController,
                             QWidget *parent = nullptr);

  /// @brief Refresh the tree from MarkerManager's loaded packs
  void refreshTree();

  /// @brief Handle dropped marker pack files (called from MainWindow)
  void handleDroppedFiles(const QList<QUrl> &urls);

protected:
  void changeEvent(QEvent *event) override;

private:
  void setupUI();
  void buildOnlinePacksSection(QVBoxLayout *mainLayout);
  void updateSectionStyles();
  void rebuildCards();
  QWidget *createPackCard(const OnlineMarkerPack &pack, bool installed);
  void applyCardTheme(QWidget *card);
  void onPackStatusChanged(const QString &packId);
  void installDroppedFiles(const QList<QUrl> &urls);
  QWidget *createUserPackCard(const QString &filename);
  void addCategoryItems(QTreeWidgetItem *parent,
                        const QList<MarkerCategory> &categories);

  DataService *m_dataService;
  MarkerController *m_markerController;
  MarkerPackRegistry *m_registry = nullptr;

  // Online packs UI
  // Splitter removed — using QHBoxLayout for installed/available panels
  QWidget *m_installedContainer = nullptr;
  QVBoxLayout *m_installedLayout = nullptr;
  QWidget *m_availableContainer = nullptr;
  QVBoxLayout *m_availableLayout = nullptr;
  QLabel *m_installedLabel = nullptr;
  QLabel *m_availableLabel = nullptr;
  QLabel *m_dropHintLabel = nullptr;
  LabeledToggle *m_autoUpdateToggle = nullptr;
  QPushButton *m_checkUpdatesBtn = nullptr;

  // Local packs tree
  QTreeWidget *m_tree;
  QLabel *m_statusLabel;
  QPushButton *m_openFolderBtn;
  QPushButton *m_reloadBtn;
};

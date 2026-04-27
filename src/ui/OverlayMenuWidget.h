#pragma once

/**
 * @brief QPainter-based in-game overlay menu (Blish HUD style)
 *
 * Renders a corner icon in the top-left and an expandable panel with
 * pack/category toggles and overlay settings. All rendering is done
 * via QPainter + manual hit-testing (no Qt widgets inside the overlay).
 *
 * This approach matches TacO's Whiteboard and Blish HUD's UI framework:
 * - Robust with WS_EX_TRANSPARENT click-through toggling
 * - No focus stealing from the game
 * - Portable rendering (not dependent on OS widget systems)
 *
 * Consumers:
 * - OverlayWindow: owns this widget, connects menuToggled signal
 *
 * DO NOT ADD:
 * - Qt widgets (QPushButton, QTreeWidget, etc.) — use QPainter only
 * - Marker rendering (belongs in MarkerRenderer/GLMarkerRenderer)
 * - Inline implementations (use OverlayMenuWidget.cpp)
 */

#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QWidget>

class MarkerManager;
class MarkerSettingsManager;
struct MarkerCategory;

/**
 * @brief Tree item for the overlay menu (QPainter-rendered, NOT
 * QTreeWidgetItem)
 */
struct OverlayMenuItem {
  QString id;       // Pack ID or category fullName
  QString label;    // Display text
  QString iconPath; // Category icon (absolute disk path, may be empty)
  bool isExpanded = false;
  bool isEnabled = true;
  bool isPack = false;      // true = top-level pack, false = category
  bool hasContent = true;   // false = no markers/trails in this subtree
  bool isSeparator = false; // section header (e.g., "[-CORE GAME-]")
  int depth = 0;            // Nesting level for indentation
  QList<OverlayMenuItem> children;
};

class OverlayMenuWidget : public QWidget {
  Q_OBJECT

public:
  explicit OverlayMenuWidget(QWidget *parent = nullptr);

  /**
   * @brief Wire data sources (call before use)
   */
  void setMarkerManager(MarkerManager *manager);
  void setMarkerSettings(MarkerSettingsManager *settings);

  // Fade visibility (called by OverlayWindow based on game state)
  void setShouldBeVisible(bool visible);
  void setCombatHidden(bool hidden);

  // Focus state: when GW2 window is not foreground, show paused icon
  void setGameFocused(bool focused);

  // Accessor for settings (used by OverlayWindow for hideInCombat check)
  MarkerSettingsManager *markerSettings() const { return m_markerSettings; }

  /**
   * @brief Open/close the menu panel
   */
  void setMenuOpen(bool open);
  bool isMenuOpen() const;

  /**
   * @brief Check if a point (in widget coordinates) is over an interactive area
   * Used by OverlayWindow for WS_EX_TRANSPARENT toggling
   */
  bool isPointOverInteractiveArea(const QPointF &pos) const;

  /**
   * @brief Rebuild the tree model from current pack data
   */
  void rebuildTree();

signals:
  /**
   * @brief Emitted when menu opens/closes (for click-through toggle)
   */
  void menuToggled(bool open);

  /**
   * @brief Emitted when user clicks "Edit Zones" (to open ExclusionZoneEditor)
   */
  void editExclusionZonesRequested();

  /**
   * @brief Emitted when user toggles the Details Tracker button
   */
  void detailsTrackerToggled(bool visible);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  // --- Drawing helpers ---
  void drawCornerIcon(QPainter &painter);
  void drawPanel(QPainter &painter);
  void drawTabBar(QPainter &painter, const QRectF &tabArea);
  void drawTreeItem(QPainter &painter, const OverlayMenuItem &item,
                    int &yOffset, int visibleIndex);
  void drawToggleIndicator(QPainter &painter, const QRectF &rect, bool enabled);
  void drawSettingsPage(QPainter &painter, const QRectF &contentArea);
  void drawSlider(QPainter &painter, const QRectF &sliderRect, qreal value,
                  const QString &label);

  // --- Hit testing ---
  int hitTestTreeItem(const QPointF &pos) const;
  bool hitTestToggle(const QPointF &pos, int itemIndex) const;
  bool hitTestExpander(const QPointF &pos, int itemIndex) const;

  // --- Tree flattening (for rendering/hit-test) ---
  void flattenTree();
  void flattenItem(OverlayMenuItem &item);

  // --- Tree building helpers ---
  void addCategoryToModel(QList<OverlayMenuItem> &parentChildren,
                          const QString &packId, const MarkerCategory &cat,
                          int depth, const QSet<QString> &contentPaths);
  QString findPackIdForItem(int flatIndex) const;

  // --- Data ---
  MarkerManager *m_markerManager = nullptr;
  MarkerSettingsManager *m_markerSettings = nullptr;

  // --- State ---
  bool m_isMenuOpen = false;
  bool m_suppressRebuild = false; // Prevent rebuildTree during own toggle ops
  int m_hoverIndex = -1;          // Flattened tree index under cursor
  int m_scrollOffset = 0;
  int m_maxScroll = 0;
  int m_settingsScrollOffset = 0;   // Settings tab scroll offset
  int m_settingsMaxScroll = 0;      // Settings tab max scroll

  // --- Tab state ---
  enum class Tab { Packs, Settings };
  Tab m_activeTab = Tab::Packs;
  QRectF m_packsTabRect;
  QRectF m_settingsTabRect;

  // --- Slider drag state ---
  bool m_isDraggingSlider = false;
  int m_dragSliderIndex =
      -1; // 0 = overlay opacity, 1 = minimap opacity, 2 = fade edge
  QRectF m_overlaySliderRect;
  QRectF m_minimapSliderRect;
  QRectF m_fadeEdgeSliderRect;

  // --- Exclusion zone toggle rects (hit-test areas) ---
  QRectF m_exclusionToggleRect;
  QRectF m_minimapToggleRect;
  QRectF m_skillBarToggleRect;
  QRectF m_chatToggleRect;
  QRectF m_editZonesButtonRect;
  QRectF m_distanceToggleRect;
  QRectF m_distFontSliderRect;
  QRectF m_markerScaleSliderRect;
  QRectF m_distLabelOffsetSliderRect;
  QRectF m_detailsTrackerButtonRect;
  QRectF m_closeButtonRect;
  bool m_detailsTrackerVisible = false;
  QRectF m_combatToggleRect;       // Hide in combat toggle hit area
  QRectF m_showInBigMapToggleRect; // Show in big map toggle hit area
  QRectF m_renderDistSliderRect;   // Render distance slider hit area
  QRectF m_heightFilterToggleRect; // Height filter toggle hit area
  QRectF m_heightRangeSliderRect;  // Height range slider hit area
  QRectF m_trailWidthSliderRect;          // Trail width slider hit area
  QRectF m_minimapMarkerScaleSliderRect;  // Minimap marker scale slider
  QRectF m_minimapMarkerOpacitySliderRect; // Minimap marker opacity slider

  // Rendering layer toggle hit areas
  QRectF m_mainRenderToggleRect;
  QRectF m_3dRenderToggleRect;
  QRectF m_minimapRenderToggleRect;
  QRectF m_bigMapRenderToggleRect;

  // --- Geometry (computed in paintEvent) ---
  QRectF m_iconRect;        // Corner icon hit area
  QRectF m_panelRect;       // Panel area
  QRectF m_panelHeaderRect; // Panel header (drag area)

  // --- Panel drag ---
  bool m_isDraggingPanel = false;
  QPointF m_panelDragStart;
  QPointF m_panelPos{-1,
                     -1}; // Dynamic panel position (-1 = default below icon)

  // --- Tree model ---
  QList<OverlayMenuItem> m_treeRoots; // Hierarchical tree
  QList<OverlayMenuItem *>
      m_flatItems; // Flattened visible items (for rendering)

  // --- Layout constants ---
  static constexpr int kIconSize = 32;
  static constexpr int kIconX =
      320; // Blish formula: ICON_SIZE(32) * ICON_POSITION(10)
  static constexpr int kIconY = 0; // Top of client area (y=0, same as Blish)
  static constexpr int kPanelWidth = 360;
  static constexpr int kItemHeight = 28;
  static constexpr int kIndentStep = 20;
  static constexpr int kToggleWidth = 16;
  static constexpr int kPanelHeaderHeight = 36;
  static constexpr int kTabBarHeight = 28;
  static constexpr int kSliderHeight = 24;
  static constexpr int kSliderTrackHeight = 6;
  static constexpr int kMaxPanelHeight = 1200;
  static constexpr int kCloseButtonSize = 16;

  // --- Fade state ---
  qreal m_opacity = 1.0;                      // Current opacity (0.0 – 1.0)
  bool m_shouldBeVisible = true;              // Target visibility state
  static constexpr qreal kFadeInStep = 0.03;  // ~600ms at 50Hz
  static constexpr qreal kFadeOutStep = 1.0;  // Instant hide on loading/char-select

  // --- Combat fade (panel only, diamond stays visible) ---
  qreal m_panelCombatOpacity = 1.0;
  bool m_combatHidden = false;

  // --- Focus state (multibox: unfocused shows paused icon) ---
  bool m_gameFocused = true;
};

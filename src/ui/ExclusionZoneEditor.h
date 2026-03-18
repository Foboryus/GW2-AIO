#pragma once

/**
 * @brief Transparent overlay editor for custom exclusion zones
 *
 * Shows as a full-screen transparent overlay on top of GW2.
 * User can click-drag to create new exclusion zone rectangles.
 * Existing zones are shown with semi-transparent tinted overlays
 * and a delete button. Escape or the close button exits.
 *
 * All rendering via QPainter (no Qt widgets) — same pattern
 * as OverlayMenuWidget for robustness with WS_EX_TRANSPARENT.
 */

#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

class MarkerSettingsManager;
class MumbleLink;

struct EditorZone {
  enum Type { Custom, Predefined, MinimapRef };
  Type type = Custom;
  QString name;
  QString predefinedKey;   // e.g., "SkillBar", "Chat" (for Predefined type)
  QRectF rect;             // Pixel coordinates (screen-space)
  QRectF deleteButtonRect; // Computed during paint (trash icon for Custom,
                           // reset for Predefined)
  QRectF editButtonRect;   // Computed during paint (edit icon)
};

class ExclusionZoneEditor : public QWidget {
  Q_OBJECT

public:
  explicit ExclusionZoneEditor(QWidget *parent = nullptr);

  void setMarkerSettings(MarkerSettingsManager *settings);
  void setMumbleLink(MumbleLink *mumble);

  /**
   * @brief Show the editor, loading current zones from settings
   */
  void beginEditing();

  /**
   * @brief Hide the editor, saving zones back to settings
   */
  void finishEditing();

signals:
  void editingFinished();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  // Convert between percentage (0..1) and screen pixels
  QRectF percentToPixels(float x, float y, float w, float h) const;
  void pixelsToPercent(const QRectF &px, float &x, float &y, float &w,
                       float &h) const;

  void saveZonesToSettings();
  void loadZonesFromSettings();

  // --- Hit-test helpers ---
  int hitTestEdges(const QRectF &r, const QPointF &pos) const;
  Qt::CursorShape cursorForEdges(int edges) const;

  // --- Data ---
  MarkerSettingsManager *m_markerSettings = nullptr;
  MumbleLink *m_mumbleLink = nullptr;
  QVector<EditorZone> m_zones;
  int m_nextZoneId = 1; // Monotonically increasing, prevents duplicate names

  // --- Interaction mode ---
  enum class Mode { None, Creating, Moving, Resizing };
  Mode m_mode = Mode::None;

  // Creating state
  QPointF m_dragStart;
  QRectF m_dragRect;

  // Moving state
  int m_moveZoneIndex = -1;
  QPointF m_moveOffset; // Offset from zone top-left to mouse

  // Resizing state
  int m_resizeZoneIndex = -1;
  enum Edge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8 };
  int m_resizeEdges = 0;       // Bitmask of edges being dragged
  QRectF m_resizeOriginalRect; // Original rect before resize
  QPointF m_resizeStart;       // Mouse position at resize start

  // --- Close button (painted, hit-tested manually) ---
  QRectF m_closeBtnRect;

  // --- Hover state ---
  int m_hoverDeleteIndex = -1;

  // --- Layout constants ---
  static constexpr int kDeleteBtnSize = 20;
  static constexpr int kIconBtnSize = 28; // Size for centered trash/edit icons
  static constexpr int kIconGap = 8;      // Gap between edit and trash icons
  static constexpr int kMinZoneSize = 20; // Minimum drag size in pixels
  static constexpr int kMaxCustomZones = 5;
};

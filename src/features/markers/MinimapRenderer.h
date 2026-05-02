#pragma once

/**
 * @brief QPainter-based 2D renderer for markers/trails on the minimap and big
 * map
 *
 * Overlays markers on GW2's compass and full map using CompassData
 * transformation. Uses buildTransformationMatrix() to convert world → screen
 * pixel coordinates within the minimap rectangle.
 *
 * Rendering modes:
 * - Minimap (compass): Rotates with camera, clipped to compass rect
 * - Big map: No rotation, covers screen area, uses mapCenterX/Y
 *
 * Architecture: This is a CHILD WIDGET of OverlayWindow (like TacO renders
 * minimap markers in the same overlay). It fills the entire OverlayWindow and
 * paints only onto the compass/map area. Transparent pixels pass through.
 * Rendering is driven by MumbleLink::dataUpdated() for zero-latency data sync.
 * Exclusion zones do NOT apply — those are for 3D rendering only (HLSL).
 *
 * DO NOT ADD:
 * - 3D rendering (belongs in GLMarkerRenderer / D3D11 pipeline)
 * - Marker management (belongs in MarkerManager)
 * - Standalone window flags (this is a child widget)
 * - Inline implementations (use MinimapRenderer.cpp)
 */

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPixmap>
#include <QWidget>

class MarkerManager;
class MumbleLink;
class ImageCache;
struct Marker;
struct Trail;
struct MarkerQueryContext;

class MinimapRenderer : public QWidget {
  Q_OBJECT

public:
  explicit MinimapRenderer(MarkerManager *manager, MumbleLink *mumble,
                           ImageCache *imageCache, QWidget *parent = nullptr);

  /**
   * @brief Start rendering — connect to MumbleLink for signal-driven repaints
   */
  void start();

  /**
   * @brief Stop rendering — disconnect signals and hide
   */
  void stop();

  // Rendering options
  void setShowMinimapMarkers(bool show);
  void setShowBigMapMarkers(bool show);
  void setOpacity(float opacity);
  void setMinimapMarkerScale(float scale);
  void setMinimapMarkerOpacity(float opacity);
  void setShouldBeVisible(bool visible);
  void setInCombat(bool combat);
  void setRenderingEnabled(bool enabled);

  /// Per-instance query context (Phase 7a) — null = use shared state
  void setQueryContext(const MarkerQueryContext *ctx) { m_queryCtx = ctx; }

  /**
   * @brief Render minimap content to a QImage (for SharedTexture upload)
   *
   * Paints the same content as paintEvent but to the provided QImage.
   * The QImage dimensions represent the GW2 window size (used for
   * compass position calculation). The image must be pre-allocated
   * with Format_ARGB32_Premultiplied.
   *
   * @param target Target QImage (must be pre-allocated, correct size)
   * @return true if content was rendered, false if skipped (not connected, etc.)
   */
  bool renderToImage(QImage &target);

protected:
  void paintEvent(QPaintEvent *event) override;
  void changeEvent(QEvent *event) override;

private slots:
  void onDataUpdated();

private:
  // Core painting logic — used by both paintEvent and renderToImage
  void renderContent(QPainter &painter, int screenW, int screenH);

  // Determine minimap screen rectangle from Mumble data
  QRectF computeMinimapRect(int screenW, int screenH) const;

  // Draw a single marker dot/icon on the minimap
  void drawMinimapMarker(QPainter &painter, const Marker &marker,
                         const QMatrix4x4 &transform, const QRectF &clipRect,
                         float mapScale);

  // Draw a trail as a polyline on the minimap
  void drawMinimapTrail(QPainter &painter, const Trail &trail,
                        const QMatrix4x4 &transform, const QRectF &clipRect);

  // Clamp a point to the edge of the minimap rect (for keepOnMapEdge markers)
  QPointF clampToEdge(const QPointF &point, const QRectF &rect) const;

  // Load marker icon as QPixmap (cached)
  QPixmap getIcon(const QString &iconPath);

  MarkerManager *m_manager;
  MumbleLink *m_mumble;
  ImageCache *m_imageCache;
  QElapsedTimer m_elapsedTimer; // For trail UV scroll animation timing

  // Options
  bool m_showMinimapMarkers = true;
  bool m_showBigMapMarkers = true;
  float m_opacity = 1.0f;
  float m_minimapMarkerScale = 1.0f;
  float m_minimapMarkerOpacity = 1.0f;
  bool m_renderingEnabled = true;

  // Fade state (per-frame stepping in paintEvent — see HideUI.md)
  bool m_shouldBeVisible = true;
  qreal m_fadeOpacity = 1.0;
  static constexpr qreal kFadeInStep = 0.03;     // ~600ms at 50Hz (loading)
  static constexpr qreal kFadeOutStep = 1.0;     // Instant hide on loading/char-select (Phase 5.8)
  static constexpr qreal kMapFadeInStep = 0.15;  // ~130ms at 50Hz (map toggle)
  static constexpr qreal kMapFadeOutStep = 0.12; // ~170ms at 50Hz (map toggle)
  bool m_mapFading = false; // True during map open/close transition

  // Map state tracking (for fade on M key)
  bool m_lastMapOpen = false;

  // Combat state (red border indicator, not hide)
  bool m_inCombat = false;

  // Frame guard: cap minimap repaints at ~60fps (16ms)
  qint64 m_lastPaintMs = 0;

  // Smoothed facing angle for player indicator arrow (exponential lerp)
  qreal m_smoothedFacingAngle = 0.0;

  // Per-instance query context (Phase 7a) — not owned
  const MarkerQueryContext *m_queryCtx = nullptr;
};

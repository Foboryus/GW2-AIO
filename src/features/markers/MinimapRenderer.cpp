/**
 * @file MinimapRenderer.cpp
 * @brief QPainter-based 2D renderer for markers/trails on the minimap
 *
 * Uses CompassData::buildTransformationMatrix() to convert world coordinates
 * → minimap pixel coordinates. Renders markers as icons/dots and trails as
 * polylines.
 *
 * Architecture: Child widget of OverlayWindow — inherits position, z-order,
 * and visibility from parent. No standalone window management needed.
 *
 * Exclusion zones do NOT apply — those are for 3D rendering only (HLSL).
 *
 * DO NOT ADD:
 * - 3D rendering logic (belongs in GLMarkerRenderer)
 * - Marker management (belongs in MarkerManager)
 * - Standalone window flags (this is a child widget)
 */

#include "MinimapRenderer.h"
#include "ImageCache.h"
#include "MarkerManager.h"
#include "MarkerModels.h"
#include "core/MumbleLink.h"

#include <QDebug>
#include <QEvent>
#include <QMatrix4x4>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MinimapRenderer::MinimapRenderer(MarkerManager *manager, MumbleLink *mumble,
                                 ImageCache *imageCache, QWidget *parent)
    : QWidget(parent), m_manager(manager), m_mumble(mumble),
      m_imageCache(imageCache) {
  // Transparent child — draws only on compass/map area, rest passes through
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_TransparentForMouseEvents);

  // No window flags — this is a child widget, not a top-level window
  // Rendering is driven by MumbleLink::dataUpdated signal (connected in
  // start())
}

void MinimapRenderer::start() {
  qInfo() << "MinimapRenderer: start() — beginning minimap rendering as child"
             " widget"
          << "parent:"
          << (parentWidget() ? parentWidget()->objectName() : "NONE")
          << "parentSize:"
          << (parentWidget() ? parentWidget()->size() : QSize());
  m_elapsedTimer.start();
  if (m_mumble) {
    connect(m_mumble, &MumbleLink::dataUpdated, this,
            &MinimapRenderer::onDataUpdated, Qt::UniqueConnection);
  }
  show();
}

void MinimapRenderer::stop() {
  qInfo() << "MinimapRenderer: stop() — hiding minimap overlay";
  if (m_mumble) {
    disconnect(m_mumble, &MumbleLink::dataUpdated, this,
               &MinimapRenderer::onDataUpdated);
  }
  hide();
}

void MinimapRenderer::onDataUpdated() {
  // Skip repaints when focus-throttled (unfocused instance)
  if (!m_renderingEnabled) return;

  // Render at full MumbleLink poll rate (~62.5Hz when focused).
  // No artificial 30fps cap — tighter tracking reduces visible drift
  // during map drag/zoom. CPU cost is negligible (QPainter markers only).
  update();
}

void MinimapRenderer::setShowMinimapMarkers(bool show) {
  m_showMinimapMarkers = show;
  update();
}

void MinimapRenderer::setShowBigMapMarkers(bool show) {
  m_showBigMapMarkers = show;
  update();
}

void MinimapRenderer::setOpacity(float opacity) {
  m_opacity = qBound(0.0f, opacity, 1.0f);
  update();
}

void MinimapRenderer::setMinimapMarkerScale(float scale) {
  m_minimapMarkerScale = qBound(0.5f, scale, 3.0f);
  update();
}

void MinimapRenderer::setMinimapMarkerOpacity(float opacity) {
  m_minimapMarkerOpacity = qBound(0.0f, opacity, 1.0f);
  update();
}

void MinimapRenderer::setRenderingEnabled(bool enabled) {
  if (m_renderingEnabled != enabled) {
    qInfo() << "[DIAG] MinimapRenderer: RENDERING_CHANGED"
            << "enabled:" << enabled
            << "shouldBeVisible:" << m_shouldBeVisible
            << "fadeOpacity:" << m_fadeOpacity;
  }
  m_renderingEnabled = enabled;
}

void MinimapRenderer::setShouldBeVisible(bool visible) {
  if (m_shouldBeVisible == visible) {
    return;
  }
  m_shouldBeVisible = visible;
  update(); // Start fade animation
}

void MinimapRenderer::changeEvent(QEvent *e) {
  if (e->type() == QEvent::StyleChange) {
    update(); // Re-read theme tokens and repaint
  }
  QWidget::changeEvent(e);
}

QRectF MinimapRenderer::computeMinimapRect(int screenW, int screenH) const {
  if (!m_mumble || !m_mumble->isConnected()) {
    return QRectF();
  }

  const CompassData &compass = m_mumble->minimapData();
  if (compass.compassWidth <= 0 || compass.compassHeight <= 0) {
    return QRectF();
  }



  // --- GW2 window-too-small scaling (TacO: GetWindowTooSmallScale) ---
  // When GW2's client area is below ~1024px wide, GW2 internally scales
  // down its UI rendering. The MumbleLink compass dimensions don't change
  // (they report the "logical" size), but the actual visible minimap shrinks.
  // We must apply the same scale factor to match the visual.
  constexpr float kMinWindowWidth = 1024.0f;
  constexpr float kMinWindowHeight = 768.0f;
  float wtsW = (screenW < kMinWindowWidth)
                   ? static_cast<float>(screenW) / kMinWindowWidth
                   : 1.0f;
  float wtsH = (screenH < kMinWindowHeight)
                   ? static_cast<float>(screenH) / kMinWindowHeight
                   : 1.0f;
  float windowTooSmallScale = qMin(wtsW, wtsH);

  float cw = static_cast<float>(compass.compassWidth) * windowTooSmallScale;
  float ch = static_cast<float>(compass.compassHeight) * windowTooSmallScale;

  float x, y;
  if (m_mumble->isMinimapTopRight()) {
    x = static_cast<float>(screenW) - cw;
    y = 1.0f * windowTooSmallScale;
  } else {
    // Default: bottom-right
    // TacO applies a 'delta' offset for the bottom game bar (skills, chat)
    // The delta varies by UI size setting
    int uiSize = m_mumble->uiSize();
    float delta = 37.0f; // Normal UI
    if (uiSize == 0)
      delta = 33.0f; // Small
    else if (uiSize == 2)
      delta = 41.0f; // Large
    else if (uiSize == 3)
      delta = 45.0f; // Larger

    delta *= windowTooSmallScale;

    x = static_cast<float>(screenW) - cw;
    y = static_cast<float>(screenH) - ch - delta;
  }

  // [DEVLOG] throttled: log window-too-small scale when it's active
  {
    static float s_lastWts = 1.0f;
    if (qAbs(windowTooSmallScale - s_lastWts) > 0.001f) {
      s_lastWts = windowTooSmallScale;
      qInfo() << "[DEVLOG] MinimapRenderer: windowTooSmallScale:"
              << windowTooSmallScale
              << "screenW:" << screenW << "screenH:" << screenH
              << "cw:" << cw << "ch:" << ch;
    }
  }

  return QRectF(x, y, cw, ch);
}

void MinimapRenderer::paintEvent(QPaintEvent *) {
  if (!m_mumble || !m_mumble->isConnected()) {
    return;
  }

  // --- Map state change detection: trigger fade on M key ---
  bool currentMapOpen = m_mumble->isMapOpen();
  if (currentMapOpen != m_lastMapOpen) {
    m_lastMapOpen = currentMapOpen;
    m_mapFading = true;
    m_fadeOpacity = 0.0; // Reset to trigger fade-in from zero
  }

  // --- Fade animation: step opacity toward target each frame ---
  qreal fadeIn = m_mapFading ? kMapFadeInStep : kFadeInStep;
  qreal fadeOut = m_mapFading ? kMapFadeOutStep : kFadeOutStep;

  if (m_shouldBeVisible && m_fadeOpacity < 1.0) {
    m_fadeOpacity = qMin(1.0, m_fadeOpacity + fadeIn);
    update(); // Keep animating
    if (m_fadeOpacity >= 1.0) {
      m_mapFading = false; // Animation complete
    }
  } else if (!m_shouldBeVisible && m_fadeOpacity > 0.0) {
    m_fadeOpacity = qMax(0.0, m_fadeOpacity - fadeOut);
    update(); // Keep animating
  }

  // Skip painting when fully faded out
  if (m_fadeOpacity <= 0.0) {
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  renderContent(painter, width(), height());
}

bool MinimapRenderer::renderToImage(QImage &target) {
  if (!m_mumble || !m_mumble->isConnected()) {
    return false;
  }
  if (!m_renderingEnabled || target.isNull()) {
    return false;
  }

  // Update fade state (same logic as paintEvent)
  bool currentMapOpen = m_mumble->isMapOpen();
  if (currentMapOpen != m_lastMapOpen) {
    m_lastMapOpen = currentMapOpen;
    m_mapFading = true;
    m_fadeOpacity = 0.0;
  }
  qreal fadeIn = m_mapFading ? kMapFadeInStep : kFadeInStep;
  qreal fadeOut = m_mapFading ? kMapFadeOutStep : kFadeOutStep;
  if (m_shouldBeVisible && m_fadeOpacity < 1.0) {
    m_fadeOpacity = qMin(1.0, m_fadeOpacity + fadeIn);
    if (m_fadeOpacity >= 1.0) m_mapFading = false;
  } else if (!m_shouldBeVisible && m_fadeOpacity > 0.0) {
    m_fadeOpacity = qMax(0.0, m_fadeOpacity - fadeOut);
  }
  if (m_fadeOpacity <= 0.0) {
    return false;
  }

  target.fill(Qt::transparent);
  QPainter painter(&target);
  painter.setRenderHint(QPainter::Antialiasing, true);
  renderContent(painter, target.width(), target.height());
  painter.end();
  return true;
}

void MinimapRenderer::renderContent(QPainter &painter, int screenW, int screenH) {
  painter.setOpacity(static_cast<qreal>(m_opacity) * m_fadeOpacity);

  QRectF miniRect = computeMinimapRect(screenW, screenH);
  if (miniRect.isEmpty()) {
    return;
  }

  // Determine if big map is open
  bool bigMapOpen = m_mumble->isMapOpen();

  // Choose compass data based on map mode
  const CompassData &compass =
      bigMapOpen ? m_mumble->bigMapData() : m_mumble->minimapData();

  // For big map, use the full area
  QRectF renderRect = bigMapOpen ? QRectF(0, 0, screenW, screenH) : miniRect;

  // Build transformation matrix (world -> screen pixel coords in renderRect)
  bool ignoreRotation = bigMapOpen; // Big map doesn't rotate
  QMatrix4x4 transform = compass.buildTransformationMatrix(
      renderRect, m_mumble->playerPosition(), ignoreRotation);

  // Clip to the render area to avoid drawing outside minimap/bigmap
  painter.setClipRect(renderRect);

  // Trails are rendered by TrailPipeline::renderMinimap() (GPU)
  // QPainter trail drawing removed — only markers remain here

  // --- Z-order: trails (GPU, already drawn) → markers → player dot → border

  // Draw markers (respecting per-layer toggle)
  bool showMarkers = bigMapOpen ? m_showBigMapMarkers : m_showMinimapMarkers;
  if (showMarkers) {
    RenderContext markerCtx =
        bigMapOpen ? RenderContext::BigMap : RenderContext::Minimap;
    QList<const Marker *> markers = m_queryCtx
        ? m_manager->getVisibleMarkers(markerCtx, *m_queryCtx)
        : m_manager->getVisibleMarkers(markerCtx);

    // [DEVLOG] throttled: minimap paint stats (every ~60th frame)
    {
      static int s_mmPaintCount = 0;
      if (++s_mmPaintCount % 60 == 0) {
        qInfo() << "[DEVLOG] MinimapRenderer: paint"
                << "mode:" << (bigMapOpen ? "BigMap" : "Minimap")
                << "compassRect:" << renderRect
                << "markerCount:" << markers.size()
                << "mapScale:" << compass.mapScale
                << "screenSize:" << screenW << "x" << screenH;
      }
    }

    // Apply marker-specific opacity (stacked with global fade)
    qreal savedOpacity = painter.opacity();
    painter.setOpacity(savedOpacity *
                       static_cast<qreal>(m_minimapMarkerOpacity));

    for (const Marker *marker : markers) {
      if (!marker->visible)
        continue;

      drawMinimapMarker(painter, *marker, transform, renderRect,
                        compass.mapScale);
    }

    painter.setOpacity(savedOpacity); // Restore for subsequent drawing
  }


  // --- Border + player icon now drawn by ChildCompositor (Phase 5.10) ---
  // MinimapRenderer is markers + trails only.
}

void MinimapRenderer::drawMinimapMarker(QPainter &painter, const Marker &marker,
                                        const QMatrix4x4 &transform,
                                        const QRectF &clipRect,
                                        float mapScale) {
  // Transform world position to minimap pixel coordinates
  // MumbleLink uses (X, Y, Z) where Y is up — minimap uses (X, Z) plane
  QVector4D worldPos(marker.xpos, marker.ypos, marker.zpos, 1.0f);
  QVector4D screenPos = transform * worldPos;

  QPointF pt(screenPos.x(), screenPos.y());

  // Check if marker is within the minimap bounds
  bool inBounds = clipRect.contains(pt);

  if (!inBounds) {
    if (marker.keepOnMapEdge) {
      // Clamp to edge of minimap
      pt = clampToEdge(pt, clipRect);
    } else {
      return; // Don't draw markers outside the minimap
    }
  }

  // Determine render size from miniMapSize (pixels on minimap), apply scale
  // TacO: miniMapSize is in map-coordinate pixels that scale with zoom.
  // mapScale increases when zooming out — markers should get smaller.
  // Use a reference scale to normalize (minimap default ~1.0).
  float zoomFactor = (mapScale > 0.0f) ? (1.0f / mapScale) : 1.0f;
  float renderSize =
      static_cast<float>(marker.miniMapSize) * m_minimapMarkerScale * zoomFactor;
  if (renderSize <= 0)
    renderSize = 20.0f * m_minimapMarkerScale * zoomFactor;

  // [DEVLOG] Proportional clamp: prevent marker icons from exceeding 25% of
  // the compass rect's smallest dimension. Without this, small windows show
  // oversized markers that visually overflow the minimap area.
  float maxMarkerSize = static_cast<float>(
      qMin(clipRect.width(), clipRect.height())) * 0.25f;
  if (maxMarkerSize > 0.0f && renderSize > maxMarkerSize) {
    renderSize = maxMarkerSize;
  }

  // Try to load the marker icon
  QPixmap icon = getIcon(marker.iconPath);

  if (!icon.isNull()) {
    // Draw the icon scaled to minimap size
    QRectF iconRect(pt.x() - renderSize / 2.0f, pt.y() - renderSize / 2.0f,
                    renderSize, renderSize);
    painter.drawPixmap(iconRect.toRect(), icon);
  } else {
    // Fallback: draw a colored circle
    float radius = renderSize / 2.0f;
    QColor color = marker.color;
    color.setAlphaF(color.alphaF() * marker.alpha);

    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(pt, radius, radius);
  }
}

void MinimapRenderer::drawMinimapTrail(QPainter &painter, const Trail &trail,
                                       const QMatrix4x4 &transform,
                                       const QRectF &clipRect) {
  if (trail.points.size() < 2)
    return;

  Q_UNUSED(clipRect)

  // Build a path from trail points, transformed to minimap coords
  QPainterPath path;
  bool started = false;

  for (const QVector3D &worldPt : trail.points) {
    QVector4D pos(worldPt, 1.0f);
    QVector4D screenPos = transform * pos;
    QPointF pt(screenPos.x(), screenPos.y());

    if (!started) {
      path.moveTo(pt);
      started = true;
    } else {
      path.lineTo(pt);
    }
  }

  // Draw the trail line
  QColor trailColor = trail.color;
  trailColor.setAlphaF(trailColor.alphaF() * trail.alpha);

  float lineWidth = 2.0f * trail.trailScale;

  QPen pen(trailColor, lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
}

QPointF MinimapRenderer::clampToEdge(const QPointF &point,
                                     const QRectF &rect) const {
  // Clamp point to the nearest edge of the rectangle
  // while maintaining direction from center
  QPointF center = rect.center();
  qreal dx = point.x() - center.x();
  qreal dy = point.y() - center.y();

  if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy)) {
    return center;
  }

  // Calculate scaling factor to reach the edge
  qreal scaleX = !qFuzzyIsNull(dx) ? (dx > 0.0 ? (rect.right() - center.x())
                                               : (center.x() - rect.left())) /
                                         qAbs(dx)
                                   : 1e10;
  qreal scaleY = !qFuzzyIsNull(dy) ? (dy > 0.0 ? (rect.bottom() - center.y())
                                               : (center.y() - rect.top())) /
                                         qAbs(dy)
                                   : 1e10;

  qreal scale = qMin(scaleX, scaleY);

  // Only clamp if the point is actually outside
  if (scale >= 1.0) {
    return point; // Already inside
  }

  return QPointF(center.x() + dx * scale, center.y() + dy * scale);
}

QPixmap MinimapRenderer::getIcon(const QString &iconPath) {
  if (iconPath.isEmpty()) {
    return QPixmap();
  }

  if (m_imageCache) {
    // Request 64×64 max — minimap icons render at ~20px, 64 provides
    // 3× headroom for retina/zoom while saving ~15× memory vs 256×256
    return m_imageCache->getPixmap(iconPath, 64);
  }

  // Fallback: direct load (should not happen when properly wired)
  return QPixmap(iconPath);
}

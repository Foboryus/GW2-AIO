/**
 * @file MarkerRenderer.cpp
 * @brief QPainter-based fallback renderer for markers and trails
 *
 * Software fallback for overlay rendering. GLMarkerRenderer is the
 * primary GPU-accelerated renderer. This exists for compatibility
 * when OpenGL is unavailable.
 *
 * DO NOT ADD:
 * - OpenGL code (belongs in GLMarkerRenderer)
 * - Pack management (belongs in MarkerManager)
 */

#include "MarkerRenderer.h"

#include <QPainter>
#include <cmath>

MarkerRenderer::MarkerRenderer(MarkerManager *manager, MumbleLink *mumble,
                               QWidget *parent)
    : QWidget(parent,
              Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      m_manager(manager), m_mumble(mumble), m_updateTimer(new QTimer(this)) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);

  if (m_mumble) {
    connect(m_mumble, &MumbleLink::positionChanged, this,
            &MarkerRenderer::onPositionChanged);
  }
  connect(m_manager, &MarkerManager::markersChanged, this,
          &MarkerRenderer::onMarkersChanged);

  // Update at 30fps
  connect(m_updateTimer, &QTimer::timeout, this, [this]() { update(); });
  m_updateTimer->start(33);
}

void MarkerRenderer::setShowMarkers(bool show) {
  m_showMarkers = show;
  update();
}

void MarkerRenderer::setShowTrails(bool show) {
  m_showTrails = show;
  update();
}

void MarkerRenderer::setOpacity(float opacity) {
  m_opacity = opacity;
  update();
}

void MarkerRenderer::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);

  if (!m_mumble || !m_mumble->isConnected()) {
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setOpacity(m_opacity);

  // Get camera from Mumble
  m_camX = m_mumble->playerX();
  m_camY = m_mumble->playerY();
  m_camZ = m_mumble->playerZ();

  // Draw trails first (behind markers)
  if (m_showTrails) {
    for (const Trail *trail : m_manager->getVisibleTrails()) {
      drawTrail(painter, *trail);
    }
  }

  // Draw markers
  if (m_showMarkers) {
    for (const Marker *marker : m_manager->getVisibleMarkers()) {
      drawMarker(painter, *marker);
    }
  }
}

void MarkerRenderer::drawMarker(QPainter &painter, const Marker &marker) {
  // Project world position to screen
  QVector3D worldPos(marker.xpos, marker.ypos + marker.heightOffset,
                     marker.zpos);
  QPointF screenPos = worldToScreen(worldPos);

  // Check if on screen
  if (screenPos.x() < -50 || screenPos.x() > m_screenWidth + 50 ||
      screenPos.y() < -50 || screenPos.y() > m_screenHeight + 50) {
    return;
  }

  // Calculate distance for fade
  float dx = marker.xpos - m_camX;
  float dy = marker.ypos - m_camY;
  float dz = marker.zpos - m_camZ;
  float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

  // Get alpha based on distance
  float alpha = getMarkerAlpha(marker, distance);
  if (alpha <= 0)
    return;

  // Calculate size based on distance
  float size = marker.iconSize * 30.0f / (distance / 100.0f + 1);
  size = qBound(marker.minSize, size, marker.maxSize);

  // Draw marker
  painter.save();

  QColor color = marker.color;
  color.setAlphaF(color.alphaF() * alpha);

  // Simple circle marker (replace with texture when available)
  painter.setBrush(color);
  painter.setPen(QPen(color.darker(150), 2));

  QRectF rect(screenPos.x() - size / 2, screenPos.y() - size / 2, size, size);
  painter.drawEllipse(rect);

  painter.restore();
}

void MarkerRenderer::drawTrail(QPainter &painter, const Trail &trail) {
  if (trail.points.size() < 2)
    return;

  painter.save();

  QColor color = trail.color;
  color.setAlphaF(color.alphaF() * 0.7f);
  painter.setPen(QPen(color, 3 * trail.trailScale));

  QPainterPath path;
  bool started = false;

  for (const QVector3D &point : trail.points) {
    QPointF screenPos = worldToScreen(point);

    // Check if on screen
    if (screenPos.x() < -100 || screenPos.x() > m_screenWidth + 100 ||
        screenPos.y() < -100 || screenPos.y() > m_screenHeight + 100) {
      started = false;
      continue;
    }

    if (!started) {
      path.moveTo(screenPos);
      started = true;
    } else {
      path.lineTo(screenPos);
    }
  }

  painter.drawPath(path);
  painter.restore();
}

QPointF MarkerRenderer::worldToScreen(const QVector3D &worldPos) const {
  // Simple perspective projection
  // This is a simplified version — GLMarkerRenderer uses proper camera matrices

  // Vector from camera to point
  float dx = worldPos.x() - m_camX;
  float dy = worldPos.y() - m_camY;
  float dz = worldPos.z() - m_camZ;

  // Distance to point
  float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dist < 0.1f)
    return QPointF(-1000, -1000); // Too close

  // Normalize direction
  dx /= dist;
  dy /= dist;
  dz /= dist;

  // Simple projection (angle-based)
  // TODO: Use actual camera direction from Mumble
  float screenX =
      (dx / (dz + 0.001f)) * static_cast<float>(m_screenWidth) / 2.0f +
      static_cast<float>(m_screenWidth) / 2.0f;
  float screenY =
      (-dy / (dz + 0.001f)) * static_cast<float>(m_screenHeight) / 2.0f +
      static_cast<float>(m_screenHeight) / 2.0f;

  return QPointF(screenX, screenY);
}

float MarkerRenderer::getMarkerAlpha(const Marker &marker,
                                     float distance) const {
  // -1 means no fading (always visible)
  if (marker.fadeNear < 0 || marker.fadeFar < 0) {
    return 1.0f;
  }

  if (distance < marker.fadeNear) {
    return 1.0f;
  } else if (distance > marker.fadeFar) {
    return 0.0f;
  } else {
    // Linear fade
    float range = marker.fadeFar - marker.fadeNear;
    if (range <= 0)
      return 1.0f;
    return 1.0f - (distance - marker.fadeNear) / range;
  }
}

void MarkerRenderer::onPositionChanged() {
  // Position updated, redraw will happen on timer
}

void MarkerRenderer::onMarkersChanged() { update(); }

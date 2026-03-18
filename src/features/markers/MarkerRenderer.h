// REVIEW BEFORE BETA: dead code — QPainter fallback replaced by D3D11 pipeline. Remove file + CMakeLists entry.
#pragma once

#include <QPainterPath>
#include <QTimer>
#include <QVector3D>
#include <QWidget>

#include "MarkerManager.h"
#include "MarkerModels.h"
#include "core/MumbleLink.h"

/**
 * @brief QPainter-based fallback renderer for markers and trails
 *
 * Uses Mumble Link for camera position and performs world-to-screen projection.
 * This is the software fallback — GLMarkerRenderer is the primary GPU renderer.
 *
 * DO NOT ADD:
 * - Inline implementations (use MarkerRenderer.cpp)
 * - OpenGL code (belongs in GLMarkerRenderer)
 */
class MarkerRenderer : public QWidget {
  Q_OBJECT

public:
  explicit MarkerRenderer(MarkerManager *manager, MumbleLink *mumble,
                          QWidget *parent = nullptr);

  // Rendering options
  void setShowMarkers(bool show);
  void setShowTrails(bool show);
  void setOpacity(float opacity);

protected:
  void paintEvent(QPaintEvent *event) override;

private slots:
  void onPositionChanged();
  void onMarkersChanged();

private:
  QPointF worldToScreen(const QVector3D &worldPos) const;
  float getMarkerAlpha(const Marker &marker, float distance) const;
  void drawMarker(QPainter &painter, const Marker &marker);
  void drawTrail(QPainter &painter, const Trail &trail);

  MarkerManager *m_manager;
  MumbleLink *m_mumble;
  QTimer *m_updateTimer;

  // Camera data from Mumble
  float m_camX = 0, m_camY = 0, m_camZ = 0;
  float m_camFrontX = 0, m_camFrontY = 0, m_camFrontZ = 1;

  // Screen dimensions
  int m_screenWidth = 1920;
  int m_screenHeight = 1080;
  float m_fov = 1.0f; // Field of view in radians

  // Options
  bool m_showMarkers = true;
  bool m_showTrails = true;
  float m_opacity = 1.0f;
};

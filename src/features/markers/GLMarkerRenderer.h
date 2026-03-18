// REVIEW BEFORE BETA: dead code — OpenGL renderer replaced by D3D11 pipeline. Remove file + CMakeLists entry.
#pragma once

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QTimer>
#include <QVector3D>

#include "core/MumbleLink.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerModels.h"
#include "features/markers/TextureCache.h"

/**
 * @brief OpenGL-based 3D marker and trail renderer
 *
 * Uses proper camera-based 3D projection extracted from TacO's algorithm:
 * 1. Build LookAt view matrix from MumbleLink camera position + direction
 * 2. Build perspective matrix from MumbleLink FOV
 * 3. Render textured billboards for markers
 * 4. Render textured billboard-strip meshes for trails
 *
 * DO NOT ADD:
 * - Marker parsing logic (belongs in TacoParser)
 * - Marker management (belongs in MarkerManager)
 */
class GLMarkerRenderer : public QOpenGLWidget, protected QOpenGLFunctions {
  Q_OBJECT

public:
  explicit GLMarkerRenderer(MarkerManager *manager, MumbleLink *mumble,
                            QWidget *parent = nullptr);
  ~GLMarkerRenderer();

  void setShowMarkers(bool show) {
    m_showMarkers = show;
    update();
  }
  void setShowTrails(bool show) {
    m_showTrails = show;
    update();
  }
  void setOpacity(float opacity) {
    m_opacity = opacity;
    update();
  }
  void setShowDistance(bool show) {
    m_showDistance = show;
    update();
  }

  TextureCache *textureCache() { return &m_textureCache; }

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

private slots:
  void onCameraChanged();

private:
  void setupShaders();
  void updateCamera();
  void renderMarkers();
  void renderTrails();

  MarkerManager *m_manager;
  MumbleLink *m_mumble;
  QTimer *m_updateTimer;
  TextureCache m_textureCache;

  // OpenGL resources
  QOpenGLShaderProgram *m_markerShader = nullptr;
  QOpenGLShaderProgram *m_trailShader = nullptr;
  QOpenGLBuffer m_quadVBO;

  // Default white texture (for untextured markers)
  GLuint m_defaultTexture = 0;

  // Matrices (updated from MumbleLink each frame)
  QMatrix4x4 m_projection;
  QMatrix4x4 m_view;

  // Animation timing (for UV scrolling)
  QElapsedTimer m_elapsedTimer;

  // Options
  bool m_showMarkers = true;
  bool m_showTrails = true;
  bool m_showDistance = false;
  float m_opacity = 1.0f;
};

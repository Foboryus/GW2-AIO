/**
 * @file GLMarkerRenderer.cpp
 * @brief OpenGL 3D marker and trail renderer using proper camera projection
 *
 * Rendering algorithm extracted from TacO's gw2tactical.cpp:
 * 1. Build LookAt matrix from Mumble fCameraPosition + fCameraFront
 * 2. Build Perspective matrix from Mumble FOV
 * 3. Textured billboards for markers
 * 4. Textured billboard-strip meshes for trails (UV scrolling animation)
 *
 * DO NOT ADD:
 * - Marker parsing logic (belongs in TacoParser)
 * - Marker management (belongs in MarkerManager)
 */

#include "GLMarkerRenderer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <QDebug>
#include <vector>

GLMarkerRenderer::GLMarkerRenderer(MarkerManager *manager, MumbleLink *mumble,
                                   QWidget *parent)
    : QOpenGLWidget(parent), m_manager(manager), m_mumble(mumble),
      m_updateTimer(new QTimer(this)), m_quadVBO(QOpenGLBuffer::VertexBuffer) {
  // Transparent frameless window
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);

  // 60 FPS update
  connect(m_updateTimer, &QTimer::timeout, this, [this]() { update(); });
  m_updateTimer->start(16);

  // Update when camera changes (not just position)
  connect(m_mumble, &MumbleLink::cameraChanged, this,
          &GLMarkerRenderer::onCameraChanged);
  connect(m_mumble, &MumbleLink::positionChanged, this,
          &GLMarkerRenderer::onCameraChanged);
}

GLMarkerRenderer::~GLMarkerRenderer() {
  makeCurrent();
  delete m_markerShader;
  delete m_trailShader;
  m_quadVBO.destroy();
  if (m_defaultTexture) {
    glDeleteTextures(1, &m_defaultTexture);
  }
  m_textureCache.clear();
  doneCurrent();
}

void GLMarkerRenderer::initializeGL() {
  initializeOpenGLFunctions();

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  setupShaders();

  // Create quad VBO for billboards (2 triangles forming a quad)
  // Each vertex: position.xy + texcoord.xy
  float quadVertices[] = {
      // pos        // uv
      -0.5f, 0.5f, 0.0f, 1.0f,  -0.5f, -0.5f,
      0.0f,  0.0f, 0.5f, -0.5f, 1.0f,  0.0f,

      -0.5f, 0.5f, 0.0f, 1.0f,  0.5f,  -0.5f,
      1.0f,  0.0f, 0.5f, 0.5f,  1.0f,  1.0f,
  };

  m_quadVBO.create();
  m_quadVBO.bind();
  m_quadVBO.allocate(quadVertices, sizeof(quadVertices));

  // Create default 1x1 white texture (for untextured markers)
  unsigned char whitePixel[] = {255, 255, 255, 255};
  glGenTextures(1, &m_defaultTexture);
  glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               whitePixel);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Start animation timer for UV scrolling
  m_elapsedTimer.start();
}

void GLMarkerRenderer::resizeGL(int w, int h) {
  glViewport(0, 0, w, h);
  // Projection updated in paintGL from live MumbleLink FOV
}

void GLMarkerRenderer::paintGL() {
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Transparent
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!m_mumble->isConnected()) {
    return;
  }

  updateCamera();

  if (m_showTrails) {
    renderTrails();
  }
  if (m_showMarkers) {
    renderMarkers();
  }
}

void GLMarkerRenderer::setupShaders() {
  // --- Marker billboard shader (textured) ---
  m_markerShader = new QOpenGLShaderProgram(this);

  const char *markerVert = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTexCoord;

        uniform mat4 projection;
        uniform mat4 view;
        uniform vec3 billboardPos;
        uniform float size;

        out vec2 TexCoord;

        void main() {
            // Camera-facing billboard (spherical)
            vec3 cameraRight = vec3(view[0][0], view[1][0], view[2][0]);
            vec3 cameraUp = vec3(view[0][1], view[1][1], view[2][1]);

            vec3 worldPos = billboardPos
                          + cameraRight * aPos.x * size
                          + cameraUp * aPos.y * size;

            gl_Position = projection * view * vec4(worldPos, 1.0);
            TexCoord = aTexCoord;
        }
    )";

  const char *markerFrag = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;

        uniform sampler2D markerTexture;
        uniform vec4 color;

        void main() {
            vec4 texColor = texture(markerTexture, TexCoord);
            FragColor = texColor * color;
        }
    )";

  m_markerShader->addShaderFromSourceCode(QOpenGLShader::Vertex, markerVert);
  m_markerShader->addShaderFromSourceCode(QOpenGLShader::Fragment, markerFrag);
  m_markerShader->link();

  // --- Trail shader (textured billboard strips with UV scrolling) ---
  m_trailShader = new QOpenGLShaderProgram(this);

  const char *trailVert = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec2 aTexCoord;

        uniform mat4 projection;
        uniform mat4 view;

        out vec2 TexCoord;

        void main() {
            gl_Position = projection * view * vec4(aPos, 1.0);
            TexCoord = aTexCoord;
        }
    )";

  const char *trailFrag = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;

        uniform sampler2D trailTexture;
        uniform vec4 color;
        uniform float uvOffset;

        void main() {
            vec2 scrolledUV = vec2(TexCoord.x, TexCoord.y + uvOffset);
            vec4 texColor = texture(trailTexture, scrolledUV);
            FragColor = texColor * color;
        }
    )";

  m_trailShader->addShaderFromSourceCode(QOpenGLShader::Vertex, trailVert);
  m_trailShader->addShaderFromSourceCode(QOpenGLShader::Fragment, trailFrag);
  m_trailShader->link();
}

void GLMarkerRenderer::updateCamera() {
  // Build view matrix from MumbleLink camera data
  // This is THE key fix — TacO uses camera position, not player position
  QVector3D camPos = m_mumble->cameraPosition();
  QVector3D camFront = m_mumble->cameraFront();

  m_view.setToIdentity();
  m_view.lookAt(camPos, camPos + camFront, QVector3D(0, 1, 0));

  // Build perspective matrix from MumbleLink FOV
  float fov = m_mumble->fov();
  if (fov <= 0.0f) {
    fov = 1.0f; // ~57 degrees default (radians)
  }

  float aspectRatio =
      static_cast<float>(width()) / static_cast<float>(qMax(1, height()));

  m_projection.setToIdentity();
  // MumbleLink FOV is in radians, Qt perspective() expects degrees
  m_projection.perspective(static_cast<float>(fov * 180.0 / M_PI), aspectRatio,
                           0.01f, 1000.0f);
}

void GLMarkerRenderer::renderMarkers() {
  m_markerShader->bind();
  m_markerShader->setUniformValue("projection", m_projection);
  m_markerShader->setUniformValue("view", m_view);

  m_quadVBO.bind();
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));

  QVector3D playerPos = m_mumble->playerPosition();

  for (const Marker *marker : m_manager->getVisibleMarkers()) {
    QVector3D pos(marker->xpos, marker->ypos + marker->heightOffset,
                  marker->zpos);

    // Distance check (world coordinates)
    float dist = (pos - playerPos).length();

    // Fade near/far (TacO-compatible)
    float alpha = 1.0f;
    if (marker->fadeFar >= 0 && marker->fadeNear >= 0) {
      if (dist > marker->fadeFar) {
        continue;
      }
      if (dist > marker->fadeNear) {
        alpha = 1.0f - (dist - marker->fadeNear) /
                           (marker->fadeFar - marker->fadeNear);
      }
    }

    // Bind marker texture (or default white)
    GLuint tex = m_defaultTexture;
    if (!marker->iconPath.isEmpty()) {
      GLuint cachedTex = m_textureCache.getTexture(marker->iconPath);
      if (cachedTex != 0) {
        tex = cachedTex;
      }
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    m_markerShader->setUniformValue("markerTexture", 0);

    m_markerShader->setUniformValue("billboardPos", pos);
    m_markerShader->setUniformValue("size", marker->iconSize * 2.0f);
    m_markerShader->setUniformValue(
        "color", QVector4D(marker->color.redF(), marker->color.greenF(),
                           marker->color.blueF(),
                           marker->color.alphaF() * alpha * m_opacity));

    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  m_markerShader->release();
}

void GLMarkerRenderer::renderTrails() {
  if (!m_trailShader) {
    return;
  }

  m_trailShader->bind();
  m_trailShader->setUniformValue("projection", m_projection);
  m_trailShader->setUniformValue("view", m_view);

  // Time-based UV offset for scrolling animation
  float elapsedSec = static_cast<float>(m_elapsedTimer.elapsed()) / 1000.0f;
  m_trailShader->setUniformValue("uvOffset", elapsedSec);

  QVector3D camPos = m_mumble->cameraPosition();
  QVector3D camUp = m_mumble->cameraTop();
  if (camUp.isNull()) {
    camUp = QVector3D(0, 1, 0);
  }

  QVector3D playerPos = m_mumble->playerPosition();

  for (const Trail *trail : m_manager->getVisibleTrails()) {
    if (trail->points.size() < 2) {
      continue;
    }

    // Bind trail texture (or default)
    GLuint tex = m_defaultTexture;
    if (!trail->texturePath.isEmpty()) {
      GLuint cachedTex = m_textureCache.getTexture(trail->texturePath);
      if (cachedTex != 0) {
        tex = cachedTex;
      }
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    m_trailShader->setUniformValue("trailTexture", 0);

    // Trail alpha: use the trail's own alpha (no hardcoded multiplier)
    float trailAlpha = trail->color.alphaF() * trail->alpha * m_opacity;

    m_trailShader->setUniformValue(
        "color", QVector4D(trail->color.redF(), trail->color.greenF(),
                           trail->color.blueF(), trailAlpha));

    // Build billboard strip mesh:
    // For each segment, generate a quad facing the camera
    float trailWidth = 0.5f * trail->trailScale;
    float uvAccum = 0.0f;

    // 5 floats per vertex: x,y,z, u,v
    std::vector<float> vertices;
    vertices.reserve(trail->points.size() * 2 * 5);

    for (int i = 0; i < trail->points.size(); ++i) {
      const QVector3D &pt = trail->points[i];

      // Direction along trail
      QVector3D dir;
      if (i < trail->points.size() - 1) {
        dir = (trail->points[i + 1] - pt).normalized();
      } else {
        dir = (pt - trail->points[i - 1]).normalized();
      }

      // Perpendicular vector (cross direction with camera up)
      QVector3D right = QVector3D::crossProduct(dir, camUp).normalized();
      if (right.isNull()) {
        right = QVector3D::crossProduct(dir, QVector3D(0, 0, 1)).normalized();
      }
      right *= trailWidth;

      // UV coordinate — V accumulates along trail distance
      // UV scrolling is handled by the shader's uvOffset uniform
      if (i > 0) {
        uvAccum += (trail->points[i] - trail->points[i - 1]).length();
      }
      float v = uvAccum * trail->animSpeed;

      // Left vertex
      QVector3D left = pt - right;
      vertices.push_back(left.x());
      vertices.push_back(left.y());
      vertices.push_back(left.z());
      vertices.push_back(0.0f);
      vertices.push_back(v);

      // Right vertex
      QVector3D rightPt = pt + right;
      vertices.push_back(rightPt.x());
      vertices.push_back(rightPt.y());
      vertices.push_back(rightPt.z());
      vertices.push_back(1.0f);
      vertices.push_back(v);
    }

    // Draw as triangle strip
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          vertices.data());
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          vertices.data() + 3);
    glDrawArrays(GL_TRIANGLE_STRIP, 0,
                 static_cast<GLsizei>(trail->points.size() * 2));
  }

  m_trailShader->release();
}

void GLMarkerRenderer::onCameraChanged() {
  // Camera updated on next paint (driven by m_updateTimer at 60 FPS)
}

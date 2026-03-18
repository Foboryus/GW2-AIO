/**
 * @file BlishGraphicsBridge.cpp
 * @brief Graphics bridge: Translates MonoGame/XNA calls to OpenGL
 *
 * This file contains the implementation of the BlishGraphicsBridge class which
 * intercepts MonoGame rendering calls from Blish-HUD modules and translates
 * them to OpenGL for overlay rendering.
 *
 * DO NOT ADD:
 * - Module loading logic (belongs in BlishModuleLoader)
 * - Non-graphics related functionality
 * - Direct .NET interop (belongs in BlishInterop)
 */

#include "BlishGraphicsBridge.h"

BlishGraphicsBridge::BlishGraphicsBridge(QObject *parent) : QObject(parent) {}

BlishGraphicsBridge::~BlishGraphicsBridge() {
  // Cleanup textures
  for (auto *tex : m_textures) {
    delete tex;
  }

  delete m_spriteShader;
  delete m_primitiveShader;
  delete m_3dShader;
}

bool BlishGraphicsBridge::initialize() {
  initializeOpenGLFunctions();
  setupShaders();
  m_initialized = true;
  return true;
}

void BlishGraphicsBridge::beginFrame(int width, int height) {
  m_width = width;
  m_height = height;

  // Setup 2D orthographic projection
  m_projection2D.setToIdentity();
  m_projection2D.ortho(0, width, height, 0, -1, 1);

  glViewport(0, 0, width, height);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void BlishGraphicsBridge::endFrame() {
  if (m_inSpriteBatch) {
    spriteBatchEnd();
  }
}

void BlishGraphicsBridge::spriteBatchBegin() {
  m_inSpriteBatch = true;
  m_spriteBatch.clear();
  m_currentBatchTexture = -1;
}

void BlishGraphicsBridge::spriteBatchEnd() {
  flushSpriteBatch();
  m_inSpriteBatch = false;
}

void BlishGraphicsBridge::drawTexture(int textureId, const QRectF &destRect,
                                      const QRectF &sourceRect,
                                      const QColor &tint, float rotation,
                                      const QVector2D &origin,
                                      float layerDepth) {
  Q_UNUSED(rotation);
  Q_UNUSED(origin);
  Q_UNUSED(layerDepth);

  // Flush if texture changed
  if (m_currentBatchTexture != -1 && m_currentBatchTexture != textureId) {
    flushSpriteBatch();
  }
  m_currentBatchTexture = textureId;

  // Calculate UV coordinates
  QRectF uv = sourceRect;
  if (uv.isEmpty()) {
    uv = QRectF(0, 0, 1, 1);
  } else {
    // Normalize to texture size
    QSize texSize = textureSize(textureId);
    if (texSize.isValid()) {
      uv = QRectF(uv.x() / texSize.width(), uv.y() / texSize.height(),
                  uv.width() / texSize.width(), uv.height() / texSize.height());
    }
  }

  QVector4D color(tint.redF(), tint.greenF(), tint.blueF(), tint.alphaF());

  // Add quad as two triangles
  SpriteVertex v[4];
  v[0] = {QVector2D(destRect.left(), destRect.top()),
          QVector2D(uv.left(), uv.top()), color};
  v[1] = {QVector2D(destRect.right(), destRect.top()),
          QVector2D(uv.right(), uv.top()), color};
  v[2] = {QVector2D(destRect.right(), destRect.bottom()),
          QVector2D(uv.right(), uv.bottom()), color};
  v[3] = {QVector2D(destRect.left(), destRect.bottom()),
          QVector2D(uv.left(), uv.bottom()), color};

  m_spriteBatch.append(v[0]);
  m_spriteBatch.append(v[1]);
  m_spriteBatch.append(v[2]);
  m_spriteBatch.append(v[0]);
  m_spriteBatch.append(v[2]);
  m_spriteBatch.append(v[3]);
}

void BlishGraphicsBridge::drawRectangle(const QRectF &rect,
                                        const QColor &color) {
  // Use a 1x1 white texture or untextured quad
  flushSpriteBatch();

  m_primitiveShader->bind();
  m_primitiveShader->setUniformValue("projection", m_projection2D);
  m_primitiveShader->setUniformValue("color", color);

  float vertices[] = {float(rect.left()),  float(rect.top()),
                      float(rect.right()), float(rect.top()),
                      float(rect.right()), float(rect.bottom()),
                      float(rect.left()),  float(rect.bottom())};

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  m_primitiveShader->release();
}

void BlishGraphicsBridge::flushSpriteBatch() {
  if (m_spriteBatch.isEmpty())
    return;

  m_spriteShader->bind();
  m_spriteShader->setUniformValue("projection", m_projection2D);

  // Bind texture
  if (m_currentBatchTexture > 0 && m_textures.contains(m_currentBatchTexture)) {
    m_textures[m_currentBatchTexture]->bind();
  }

  // Upload and draw
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);

  const float *data =
      reinterpret_cast<const float *>(m_spriteBatch.constData());
  int stride = sizeof(SpriteVertex);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, data);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, data + 2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, data + 4);

  glDrawArrays(GL_TRIANGLES, 0, m_spriteBatch.size());

  m_spriteShader->release();
  m_spriteBatch.clear();
}

int BlishGraphicsBridge::loadTexture(const QImage &image) {
  QOpenGLTexture *tex = new QOpenGLTexture(image.mirrored());
  tex->setMinificationFilter(QOpenGLTexture::Linear);
  tex->setMagnificationFilter(QOpenGLTexture::Linear);

  int id = m_nextTextureId++;
  m_textures[id] = tex;

  return id;
}

int BlishGraphicsBridge::loadTexture(const QString &path) {
  QImage image(path);
  if (image.isNull())
    return -1;
  return loadTexture(image);
}

void BlishGraphicsBridge::unloadTexture(int textureId) {
  if (m_textures.contains(textureId)) {
    delete m_textures[textureId];
    m_textures.remove(textureId);
  }
}

QSize BlishGraphicsBridge::textureSize(int textureId) const {
  if (m_textures.contains(textureId)) {
    return QSize(m_textures[textureId]->width(),
                 m_textures[textureId]->height());
  }
  return QSize();
}

void BlishGraphicsBridge::setupShaders() {
  // Sprite shader
  m_spriteShader = new QOpenGLShaderProgram();
  m_spriteShader->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTexCoord;
        layout(location = 2) in vec4 aColor;
        
        uniform mat4 projection;
        
        out vec2 TexCoord;
        out vec4 Color;
        
        void main() {
            gl_Position = projection * vec4(aPos, 0.0, 1.0);
            TexCoord = aTexCoord;
            Color = aColor;
        }
    )");
  m_spriteShader->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec2 TexCoord;
        in vec4 Color;
        out vec4 FragColor;
        
        uniform sampler2D tex;
        
        void main() {
            FragColor = texture(tex, TexCoord) * Color;
        }
    )");
  m_spriteShader->link();

  // Primitive shader (untextured)
  m_primitiveShader = new QOpenGLShaderProgram();
  m_primitiveShader->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        uniform mat4 projection;
        void main() {
            gl_Position = projection * vec4(aPos, 0.0, 1.0);
        }
    )");
  m_primitiveShader->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        uniform vec4 color;
        out vec4 FragColor;
        void main() {
            FragColor = color;
        }
    )");
  m_primitiveShader->link();
}

void BlishGraphicsBridge::setBlendMode(BlendMode mode) {
  switch (mode) {
  case BlendMode::AlphaBlend:
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BlendMode::Additive:
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    break;
  case BlendMode::NonPremultiplied:
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BlendMode::Opaque:
    glBlendFunc(GL_ONE, GL_ZERO);
    break;
  }
}

void BlishGraphicsBridge::setWorldMatrix(const QMatrix4x4 &world) {
  m_world = world;
}
void BlishGraphicsBridge::setViewMatrix(const QMatrix4x4 &view) {
  m_view = view;
}
void BlishGraphicsBridge::setProjectionMatrix(const QMatrix4x4 &projection) {
  m_projection3D = projection;
}

void BlishGraphicsBridge::drawString(int fontId, const QString &text,
                                     const QVector2D &position,
                                     const QColor &color, float rotation,
                                     const QVector2D &origin, float scale) {
  Q_UNUSED(fontId);
  Q_UNUSED(rotation);
  Q_UNUSED(origin);
  Q_UNUSED(scale);

  // TODO: Font rendering - for now just log
  qDebug() << "DrawString:" << text << "at" << position << "color" << color; // DEV LOG — remove before release
}

void BlishGraphicsBridge::drawLine(const QVector2D &start, const QVector2D &end,
                                   const QColor &color, float thickness) {
  Q_UNUSED(thickness);

  flushSpriteBatch();

  m_primitiveShader->bind();
  m_primitiveShader->setUniformValue("projection", m_projection2D);
  m_primitiveShader->setUniformValue("color", color);

  float vertices[] = {start.x(), start.y(), end.x(), end.y()};

  glLineWidth(thickness);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
  glDrawArrays(GL_LINES, 0, 2);

  m_primitiveShader->release();
}

void BlishGraphicsBridge::drawPrimitives(PrimitiveType type,
                                         const QVector<QVector3D> &vertices,
                                         const QVector<QColor> &colors,
                                         const QVector<QVector2D> &texCoords,
                                         int textureId) {
  Q_UNUSED(colors);
  Q_UNUSED(texCoords);
  Q_UNUSED(textureId);

  if (!m_3dShader) {
    // Create simple 3D shader
    m_3dShader = new QOpenGLShaderProgram();
    m_3dShader->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            uniform mat4 mvp;
            void main() {
                gl_Position = mvp * vec4(aPos, 1.0);
            }
        )");
    m_3dShader->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
            #version 330 core
            uniform vec4 color;
            out vec4 FragColor;
            void main() {
                FragColor = color;
            }
        )");
    m_3dShader->link();
  }

  m_3dShader->bind();

  QMatrix4x4 mvp = m_projection3D * m_view * m_world;
  m_3dShader->setUniformValue("mvp", mvp);
  m_3dShader->setUniformValue("color", QVector4D(1, 1, 1, 1));

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices.constData());
  glDrawArrays(toGLPrimitive(type), 0, vertices.size());

  m_3dShader->release();
}

GLenum BlishGraphicsBridge::toGLPrimitive(PrimitiveType type) {
  switch (type) {
  case PrimitiveType::PointList:
    return GL_POINTS;
  case PrimitiveType::LineList:
    return GL_LINES;
  case PrimitiveType::LineStrip:
    return GL_LINE_STRIP;
  case PrimitiveType::TriangleList:
    return GL_TRIANGLES;
  case PrimitiveType::TriangleStrip:
    return GL_TRIANGLE_STRIP;
  }
  return GL_TRIANGLES;
}

void BlishGraphicsBridge::setScissorRect(const QRect &rect) {
  glEnable(GL_SCISSOR_TEST);
  glScissor(rect.x(), m_height - rect.y() - rect.height(), rect.width(),
            rect.height());
}

void BlishGraphicsBridge::clearScissorRect() { glDisable(GL_SCISSOR_TEST); }

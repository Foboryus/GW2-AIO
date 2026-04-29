#pragma once

#include <QColor>
#include <QImage>
#include <QMap>
#include <QMatrix4x4>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>


/**
 * @brief Graphics bridge: Translates MonoGame/XNA calls to OpenGL
 *
 * Blish-HUD modules use MonoGame (XNA successor) for rendering.
 * This layer intercepts those calls and translates to OpenGL.
 *
 * DO NOT ADD:
 * - Inline implementations (use BlishGraphicsBridge.cpp)
 * - Module loading logic (belongs in BlishModuleLoader)
 * - Non-graphics related functionality
 */
class BlishGraphicsBridge : public QObject, protected QOpenGLFunctions {
  Q_OBJECT

public:
  explicit BlishGraphicsBridge(QObject *parent = nullptr);
  ~BlishGraphicsBridge();

  /**
   * @brief Initialize OpenGL resources
   */
  bool initialize();

  /**
   * @brief Begin a frame
   */
  void beginFrame(int width, int height);

  /**
   * @brief End frame and present
   */
  void endFrame();

  // ============================================
  // SpriteBatch equivalents (2D rendering)
  // ============================================

  /**
   * @brief Begin sprite batch
   */
  void spriteBatchBegin();
  void spriteBatchEnd();

  /**
   * @brief Draw textured quad (SpriteBatch.Draw)
   */
  void drawTexture(int textureId, const QRectF &destRect,
                   const QRectF &sourceRect = QRectF(),
                   const QColor &tint = Qt::white, float rotation = 0.0f,
                   const QVector2D &origin = QVector2D(0, 0),
                   float layerDepth = 0.0f);

  /**
   * @brief Draw text (SpriteBatch.DrawString)
   */
  void drawString(int fontId, const QString &text, const QVector2D &position,
                  const QColor &color = Qt::white, float rotation = 0.0f,
                  const QVector2D &origin = QVector2D(0, 0),
                  float scale = 1.0f);

  /**
   * @brief Draw filled rectangle
   */
  void drawRectangle(const QRectF &rect, const QColor &color);

  /**
   * @brief Draw line
   */
  void drawLine(const QVector2D &start, const QVector2D &end,
                const QColor &color, float thickness = 1.0f);

  // ============================================
  // 3D rendering (BasicEffect equivalent)
  // ============================================

  /**
   * @brief Set world matrix
   */
  void setWorldMatrix(const QMatrix4x4 &world);

  /**
   * @brief Set view matrix
   */
  void setViewMatrix(const QMatrix4x4 &view);

  /**
   * @brief Set projection matrix
   */
  void setProjectionMatrix(const QMatrix4x4 &projection);

  /**
   * @brief Draw 3D primitives
   */
  enum class PrimitiveType {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip
  };

  void drawPrimitives(PrimitiveType type, const QVector<QVector3D> &vertices,
                      const QVector<QColor> &colors = {},
                      const QVector<QVector2D> &texCoords = {},
                      int textureId = -1);

  // ============================================
  // Texture management
  // ============================================

  /**
   * @brief Load texture from image
   * @return Texture ID
   */
  int loadTexture(const QImage &image);

  /**
   * @brief Load texture from file
   */
  int loadTexture(const QString &path);

  /**
   * @brief Unload texture
   */
  void unloadTexture(int textureId);

  /**
   * @brief Get texture size
   */
  QSize textureSize(int textureId) const;

  // ============================================
  // State management
  // ============================================

  /**
   * @brief Set blend mode
   */
  enum class BlendMode { AlphaBlend, Additive, NonPremultiplied, Opaque };
  void setBlendMode(BlendMode mode);

  /**
   * @brief Set scissor rectangle
   */
  void setScissorRect(const QRect &rect);
  void clearScissorRect();

private:
  void setupShaders();
  void flushSpriteBatch();
  GLenum toGLPrimitive(PrimitiveType type);

  // Shaders
  QOpenGLShaderProgram *m_spriteShader = nullptr;
  QOpenGLShaderProgram *m_primitiveShader = nullptr;
  QOpenGLShaderProgram *m_3dShader = nullptr;

  // Textures
  QMap<int, QOpenGLTexture *> m_textures;
  int m_nextTextureId = 1;

  // Matrices
  QMatrix4x4 m_projection2D;
  QMatrix4x4 m_world;
  QMatrix4x4 m_view;
  QMatrix4x4 m_projection3D;

  // Screen size
  int m_width = 1920;
  int m_height = 1080;

  // Sprite batch
  struct SpriteVertex {
    QVector2D position;
    QVector2D texCoord;
    QVector4D color;
  };
  QVector<SpriteVertex> m_spriteBatch;
  int m_currentBatchTexture = -1;
  bool m_inSpriteBatch = false;

  bool m_initialized = false;
};

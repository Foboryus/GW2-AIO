/**
 * @file TextureCache.cpp
 * @brief Caches OpenGL textures loaded from files or raw image data
 *
 * Used by GLMarkerRenderer to load marker icons and trail textures
 * from .taco ZIP archives or standalone image files.
 *
 * When ImageCache is wired, delegates QImage loading to it (shared cache).
 * Otherwise falls back to direct file/data loading.
 */

#include "TextureCache.h"
#include "ImageCache.h"

#include <QDebug>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

TextureCache::TextureCache() {}

TextureCache::~TextureCache() { clear(); }

void TextureCache::setImageCache(ImageCache *cache) { m_imageCache = cache; }

GLuint TextureCache::getTexture(const QString &filePath) {
  if (m_textures.contains(filePath)) {
    return m_textures[filePath];
  }

  GLuint tex = loadTextureFromFile(filePath);
  if (tex != 0) {
    m_textures[filePath] = tex;
  }
  return tex;
}

GLuint TextureCache::getTextureFromData(const QString &key,
                                        const QByteArray &imageData) {
  if (m_textures.contains(key)) {
    return m_textures[key];
  }

  GLuint tex = loadTextureFromData(imageData);
  if (tex != 0) {
    m_textures[key] = tex;
  }
  return tex;
}

bool TextureCache::contains(const QString &key) const {
  return m_textures.contains(key);
}

void TextureCache::remove(const QString &key) {
  if (m_textures.contains(key)) {
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (ctx) {
      ctx->functions()->glDeleteTextures(1, &m_textures[key]);
    }
    m_textures.remove(key);
  }
}

void TextureCache::clear() {
  QOpenGLContext *ctx = QOpenGLContext::currentContext();
  if (ctx) {
    QOpenGLFunctions *f = ctx->functions();
    for (auto it = m_textures.begin(); it != m_textures.end(); ++it) {
      f->glDeleteTextures(1, &it.value());
    }
  }
  m_textures.clear();
}

GLuint TextureCache::loadTextureFromFile(const QString &filePath) {
  QImage image;

  if (m_imageCache) {
    // Use shared cache — single load from disk
    image = m_imageCache->getImage(filePath);
  } else {
    // Fallback: direct load
    image = QImage(filePath);
  }

  if (image.isNull()) {
    if (!m_imageCache) {
      qWarning() << "TextureCache: Failed to load image:" << filePath;
    }
    return 0;
  }

  return uploadImage(image);
}

GLuint TextureCache::loadTextureFromData(const QByteArray &imageData) {
  // Note: ImageCache integration for data-based loading happens at the
  // getTextureFromData() level. The caller passes a key, so we'd need
  // the key here too. For now, data-based textures are loaded directly
  // since the QByteArray is already in memory (from ZIP extraction).
  QImage image;
  if (!image.loadFromData(imageData)) {
    qWarning() << "TextureCache: Failed to load image from data";
    return 0;
  }

  return uploadImage(image);
}

GLuint TextureCache::uploadImage(const QImage &image) {
  // Convert to RGBA for OpenGL (Y-flipped for GL coordinate system)
  QImage glImage = image.convertToFormat(QImage::Format_RGBA8888).mirrored();

  QOpenGLContext *ctx = QOpenGLContext::currentContext();
  if (!ctx) {
    qWarning() << "TextureCache: No current OpenGL context";
    return 0;
  }

  QOpenGLFunctions *f = ctx->functions();

  GLuint textureId = 0;
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);

  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glImage.width(), glImage.height(),
                  0, GL_RGBA, GL_UNSIGNED_BYTE, glImage.constBits());

  return textureId;
}

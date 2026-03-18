// REVIEW BEFORE BETA: dead code — OpenGL texture cache replaced by D3D11 pipeline. Remove file + CMakeLists entry.
#pragma once

#include <QHash>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QString>

class QOpenGLContext;
class ImageCache;

/**
 * @brief Caches OpenGL textures loaded from files or ZIP archives
 *
 * Loads PNG/JPG images from disk or from within .taco ZIP files
 * and creates OpenGL textures for rendering. Textures are cached
 * by their source path to avoid redundant loading.
 *
 * DO NOT ADD:
 * - Rendering logic (belongs in GLMarkerRenderer)
 * - ZIP extraction (uses QByteArray from TacoParser)
 */
class TextureCache {
public:
  TextureCache();
  ~TextureCache();

  /**
   * @brief Set shared image cache (optional — falls back to direct load)
   */
  void setImageCache(ImageCache *cache);

  /**
   * @brief Get or load a texture from a file path
   * @param filePath Absolute path to the image file
   * @return OpenGL texture ID, or 0 if failed
   */
  GLuint getTexture(const QString &filePath);

  /**
   * @brief Get or load a texture from raw image data (e.g., from ZIP)
   * @param key Cache key (e.g., "packname/Data/icon.png")
   * @param imageData Raw image bytes (PNG, JPG, etc.)
   * @return OpenGL texture ID, or 0 if failed
   */
  GLuint getTextureFromData(const QString &key, const QByteArray &imageData);

  /**
   * @brief Check if a texture is already cached
   */
  bool contains(const QString &key) const;

  /**
   * @brief Remove a texture from cache and free GL resources
   */
  void remove(const QString &key);

  /**
   * @brief Clear all cached textures
   */
  void clear();

  /**
   * @brief Number of cached textures
   */
  int size() const { return m_textures.size(); }

private:
  GLuint loadTextureFromFile(const QString &filePath);
  GLuint loadTextureFromData(const QByteArray &imageData);
  GLuint uploadImage(const QImage &image);

  QHash<QString, GLuint> m_textures;
  ImageCache *m_imageCache = nullptr;
};

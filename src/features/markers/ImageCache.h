#pragma once

#include <QSet>

/**
 * @brief Shared in-memory image cache for marker icons and trail textures
 *
 * Loads images once from disk or ZIP data, then provides both QImage
 * (for GL texture upload) and QPixmap (for QPainter rendering) from the
 * single cached source.
 *
 * Consumers:
 * - TextureCache: calls getImage() to create GL textures
 * - MinimapRenderer: calls getPixmap() for 2D minimap icons
 *
 * Owned by MarkerController (dependency injection).
 *
 * Phase 3: Add setCacheDir() for disk-level caching of ZIP-extracted icons.
 *
 * DO NOT ADD:
 * - GL texture creation (belongs in TextureCache)
 * - Rendering logic (belongs in renderers)
 */

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QString>

class ImageCache : public QObject {
  Q_OBJECT

public:
  explicit ImageCache(QObject *parent = nullptr);

  /**
   * @brief Get or load a QImage from a file path
   * @param filePath Absolute path to the image file (used as cache key)
   * @return Loaded QImage, or null QImage if loading failed
   */
  QImage getImage(const QString &filePath);

  /**
   * @brief Get or load a QImage from raw data (e.g., from ZIP archive)
   * @param key Cache key (e.g., "packname/Data/icon.png")
   * @param imageData Raw PNG/JPG bytes
   * @return Loaded QImage, or null QImage if loading failed
   */
  QImage getImageFromData(const QString &key, const QByteArray &imageData);

  /**
   * @brief Get a QPixmap for a previously loaded image
   * Lazily creates QPixmap from cached QImage on first request.
   * @param key Cache key (file path or ZIP path)
   * @param maxSize If > 0, scale down QPixmap to fit within maxSize×maxSize
   *               (useful for minimap where icons render at ~20px)
   * @return QPixmap, or null QPixmap if image not cached or failed
   */
  QPixmap getPixmap(const QString &key, int maxSize = 0);

  /**
   * @brief Check if an image is cached
   */
  bool contains(const QString &key) const;

  /**
   * @brief Evict a QImage from the cache (free CPU memory)
   *
   * Called by D3D11 pipelines after uploading to GPU SRV.
   * The QPixmap (if cached) is kept — it's used by MinimapRenderer.
   * If getImage() is called again for an evicted key, it reloads from disk.
   */
  void evict(const QString &key);

  /**
   * @brief Clear all cached images and pixmaps
   */
  void clear();

  /**
   * @brief Number of cached images
   */
  int size() const { return m_images.size(); }

private:
  QHash<QString, QImage> m_images;
  QHash<QString, QPixmap> m_pixmaps;
  QSet<QString> m_evicted; // Keys evicted after GPU upload (avoids warning spam)
};

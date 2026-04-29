/**
 * @file ImageCache.cpp
 * @brief Shared in-memory image cache implementation
 *
 * Loads images once from disk or raw data, caches as QImage.
 * QPixmap is lazily derived on first request.
 */

#include "ImageCache.h"

#include <QDebug>

ImageCache::ImageCache(QObject *parent) : QObject(parent) {}

QImage ImageCache::getImage(const QString &filePath) {
  if (filePath.isEmpty()) {
    return QImage();
  }

  auto it = m_images.find(filePath);
  if (it != m_images.end()) {
    return it.value();
  }

  // Load from disk
  QImage image(filePath);
  if (image.isNull()) {
    qWarning() << "ImageCache: Failed to load image:" << filePath;
    // Cache null to avoid repeated load attempts
    m_images.insert(filePath, QImage());
    return QImage();
  }

  m_images.insert(filePath, image);
  return image;
}

QImage ImageCache::getImageFromData(const QString &key,
                                    const QByteArray &imageData) {
  if (key.isEmpty()) {
    return QImage();
  }

  auto it = m_images.find(key);
  if (it != m_images.end()) {
    return it.value();
  }

  // Load from raw bytes
  QImage image;
  if (!image.loadFromData(imageData)) {
    qWarning() << "ImageCache: Failed to load image from data:" << key;
    m_images.insert(key, QImage());
    return QImage();
  }

  m_images.insert(key, image);
  return image;
}

QPixmap ImageCache::getPixmap(const QString &key, int maxSize) {
  if (key.isEmpty()) {
    return QPixmap();
  }

  // Cache key includes maxSize to separate full-size and scaled entries
  QString cacheKey =
      maxSize > 0 ? key + QStringLiteral("@") + QString::number(maxSize) : key;

  // Check pixmap cache first
  auto pixIt = m_pixmaps.find(cacheKey);
  if (pixIt != m_pixmaps.end()) {
    return pixIt.value();
  }

  // Try to get QImage from cache, or load transiently from disk
  QImage image;
  bool transientLoad = false;

  auto imgIt = m_images.find(key);
  if (imgIt != m_images.end()) {
    image = imgIt.value();
  } else {
    // Load from file (transient — don't re-insert into m_images if evicted)
    image = QImage(key);
    transientLoad = true;
    if (!m_evicted.contains(key)) {
      // Not evicted — cache it normally
      m_images.insert(key, image);
    }
    // If evicted: use the image to create QPixmap, then let it go out of scope
  }

  if (image.isNull()) {
    m_pixmaps.insert(cacheKey, QPixmap());
    return QPixmap();
  }

  // Create QPixmap from QImage
  QPixmap pix = QPixmap::fromImage(image);

  // Scale down if maxSize specified and image exceeds it
  if (maxSize > 0 && (pix.width() > maxSize || pix.height() > maxSize)) {
    pix = pix.scaled(maxSize, maxSize, Qt::KeepAspectRatio,
                     Qt::SmoothTransformation);
  }

  m_pixmaps.insert(cacheKey, pix);
  return pix;
}

bool ImageCache::contains(const QString &key) const {
  return m_images.contains(key);
}

void ImageCache::evict(const QString &key) {
  if (m_images.remove(key)) {
    m_evicted.insert(key);
    qInfo() << "ImageCache: Evicted QImage" << key << "(GPU SRV cached)";
  }
}

void ImageCache::clear() {
  m_images.clear();
  m_pixmaps.clear();
  m_evicted.clear();
  qInfo() << "ImageCache: Cleared all cached images";
}

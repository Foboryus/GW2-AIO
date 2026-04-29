#pragma once

#include <QColor>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QObject>
#include <QString>
#include <QXmlStreamReader>

#include "MarkerModels.h"

/**
 * @brief Parses TacO marker packs (.taco files and XML)
 *
 * .taco files are ZIP archives containing:
 * - poidata.xml or categorydata.xml
 * - POIs directory with XML files
 * - Data directory with PNG textures
 * - trails directory with .trl binary trail files
 *
 * DO NOT ADD:
 * - Inline implementations (use TacoParser.cpp)
 */
class TacoParser : public QObject {
  Q_OBJECT

public:
  explicit TacoParser(QObject *parent = nullptr);

  /**
   * @brief Parse a .taco file or directory
   */
  MarkerPack parse(const QString &path);

  /**
   * @brief Set cache directory for extracted .taco archives
   * Extracted contents are stored here to avoid re-extraction on every startup.
   */
  void setCacheDir(const QString &dir);

  /**
   * @brief Enable metadata-only parsing (categories only, no markers/trails)
   * Used for disabled packs: preserves category tree for UI while skipping
   * heavyweight marker/trail data to save memory.
   */
  void setMetadataOnly(bool metadataOnly);

  /**
   * @brief Parse XML content directly
   */
  void parseXml(const QString &xmlContent, MarkerPack &pack);

  /**
   * @brief Read only the .trl header (8 bytes: version + mapId)
   *
   * Used during pack loading to get trail mapId without loading point data.
   * Full point data is loaded on demand by parseTrailFile() when the map changes.
   * @param trlPath Path to the .trl binary file
   * @param outMapId Populated with the map ID from the header
   * @return true if header was read successfully
   */
  bool parseTrailHeader(const QString &trlPath, uint32_t *outMapId);

  /**
   * @brief Parse a .trl trail file (full point data)
   * @param trlPath Path to the .trl binary file
   * @param outMapId If non-null, populated with the map ID from the .trl header
   * @return List of 3D points forming the trail
   */
  QList<QVector3D> parseTrailFile(const QString &trlPath,
                                  uint32_t *outMapId = nullptr);

  /**
   * @brief Get last error
   */
  QString lastError() const { return m_lastError; }

private:
  void parseOverlayData(QXmlStreamReader &xml, MarkerPack &pack);
  void parsePOI(QXmlStreamReader &xml, MarkerPack &pack);
  void parseTrail(QXmlStreamReader &xml, MarkerPack &pack);
  void parseCategory(QXmlStreamReader &xml, MarkerCategory &parent);

  /**
   * @brief Resolve a relative asset path to absolute using m_basePath
   * Handles backslash normalization and case-insensitive search
   */
  QString resolveAssetPath(const QString &relativePath) const;

  // Category inheritance (TacO pattern: parent → child → marker)
  void applyInheritance(MarkerPack &pack);
  void propagateCategoryInheritance(MarkerCategory &parent,
                                    MarkerCategory &child);
  void resolveMarkerInheritance(Marker &marker, const MarkerCategory &category);
  void resolveTrailInheritance(Trail &trail, const MarkerCategory &category);

  // Build flat map of category fullName → category pointer
  void buildCategoryMap(QList<MarkerCategory> &categories,
                        QHash<QString, MarkerCategory *> &map);

  // Merge incoming category into existing list by name (TacO merge pattern)
  // If a category with the same name exists at that level, children are merged
  // recursively and inheritable fields are OR-merged via fieldsSet bitmask.
  void mergeCategory(QList<MarkerCategory> &existing,
                     const MarkerCategory &incoming);

  QColor parseColor(const QString &colorStr);
  MarkerBehavior parseBehavior(int value);

  // AIO pack.json metadata (optional, enriches pack info)
  void parsePackJson(const QString &path, MarkerPack &pack);

  QString m_basePath;
  QString m_cacheDir;
  QString m_lastError;
  bool m_metadataOnly = false;
};

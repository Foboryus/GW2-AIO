/**
 * @file TacoParser.cpp
 * @brief Parses TacO marker packs (.taco files and XML)
 *
 * .taco files are ZIP archives containing:
 * - poidata.xml or categorydata.xml
 * - POIs directory with .xml files
 * - Data directory with .png textures
 * - trails directory with .trl binary trail files
 *
 * DO NOT ADD:
 * - Rendering logic (belongs in GLMarkerRenderer)
 * - Pack management (belongs in MarkerManager)
 */

#include "TacoParser.h"
#include "core/ZipExtractor.h"

#include <QDataStream>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector3D>
#include <cstdint>

TacoParser::TacoParser(QObject *parent) : QObject(parent) {}

void TacoParser::setCacheDir(const QString &dir) { m_cacheDir = dir; }

void TacoParser::setMetadataOnly(bool metadataOnly) {
  m_metadataOnly = metadataOnly;
}

MarkerPack TacoParser::parse(const QString &path) {
  MarkerPack pack;
  pack.path = path;

  QFileInfo fi(path);
  pack.id = fi.completeBaseName(); // Stable identifier from folder/file name
  pack.name = fi.completeBaseName();

  // Check if it's a .taco (ZIP) or directory
  // .taco files are ZIP archives — extract to cache dir, then parse as
  // directory
  if (fi.suffix().toLower() == "taco") {
    if (m_cacheDir.isEmpty()) {
      m_lastError = "No cache directory set for .taco extraction";
      qWarning() << "TacoParser:" << m_lastError;
      return pack;
    }

    QString cacheSubDir = QDir(m_cacheDir).filePath(fi.completeBaseName());
    bool needsExtract = true;

    // Check cache validity: compare source mtime+size vs cached metadata
    QString metaPath = QDir(cacheSubDir).filePath("_extract_meta.json");
    if (QFile::exists(metaPath)) {
      QFile metaFile(metaPath);
      if (metaFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        metaFile.close();
        QJsonObject meta = doc.object();
        qint64 cachedSize = meta.value("sourceSize").toInteger();
        QString cachedMod = meta.value("sourceModified").toString();
        QString sourceMod = fi.lastModified().toUTC().toString(Qt::ISODate);
        if (cachedSize == fi.size() && cachedMod == sourceMod) {
          needsExtract = false;
          qInfo() << "TacoParser: using cached extraction for" << fi.fileName();
        }
      }
    }

    if (needsExtract) {
      qInfo() << "TacoParser: extracting" << fi.fileName() << "to"
              << cacheSubDir;

      // Clean previous cache for this pack
      QDir(cacheSubDir).removeRecursively();

      if (!ZipExtractor::extractAll(path, cacheSubDir)) {
        m_lastError = "Failed to extract .taco: " + ZipExtractor::lastError();
        qWarning() << "TacoParser:" << m_lastError;
        return pack;
      }

      // Write cache metadata (versioned format per dev standards)
      QJsonObject meta;
      meta["type"] = "aio-extract-cache";
      meta["version"] = 1;
      meta["sourceFile"] = fi.fileName();
      meta["sourceSize"] = fi.size();
      meta["sourceModified"] = fi.lastModified().toUTC().toString(Qt::ISODate);
      meta["extractedAt"] =
          QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

      QFile metaFile(metaPath);
      if (metaFile.open(QIODevice::WriteOnly)) {
        metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
        metaFile.close();
      }

      qInfo() << "TacoParser: extraction complete for" << fi.fileName();
    }

    // Re-parse from the extracted directory (recurse)
    return parse(cacheSubDir);
  }

  m_basePath = path;

  // Look for XML files
  QDir dir(path);

  // Check for AIO pack.json metadata (optional, enriches pack info)
  QString packJsonPath = dir.filePath("pack.json");
  if (QFile::exists(packJsonPath)) {
    parsePackJson(packJsonPath, pack);
  }

  QStringList xmlFiles = dir.entryList({"*.xml"}, QDir::Files);

  // Also check POIs subdirectory
  QDir poisDir(dir.filePath("POIs"));
  if (poisDir.exists()) {
    for (const QString &file : poisDir.entryList({"*.xml"}, QDir::Files)) {
      xmlFiles.append("POIs/" + file);
    }
  }

  // Parse each XML file
  for (const QString &xmlFile : xmlFiles) {
    QString xmlPath = dir.filePath(xmlFile);
    QFile file(xmlPath);

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      parseXml(QString::fromUtf8(file.readAll()), pack);
      file.close();
    }
  }

  // If no pack.json provided a display name, try the first root category's
  // displayName. TacO packs conventionally name their root MarkerCategory
  // with the human-readable pack name (e.g., "Tekkit's World Guides").
  if (pack.name == pack.id && !pack.categories.isEmpty()) {
    const QString &rootDisplay = pack.categories.first().displayName;
    if (!rootDisplay.isEmpty() && rootDisplay != pack.id) {
      pack.name = rootDisplay;
    }
  }

  qInfo() << "Parsed pack:" << pack.name << "Markers:" << pack.markerCount()
          << "Trails:" << pack.trailCount();

  // Apply category inheritance: propagate parent attributes to children,
  // then resolve each marker's unset fields from its category chain
  applyInheritance(pack);

  return pack;
}

void TacoParser::parseXml(const QString &xmlContent, MarkerPack &pack) {
  QXmlStreamReader xml(xmlContent);

  while (!xml.atEnd() && !xml.hasError()) {
    QXmlStreamReader::TokenType token = xml.readNext();

    if (token == QXmlStreamReader::StartElement) {
      if (xml.name() == QString("OverlayData")) {
        parseOverlayData(xml, pack);
      }
    }
  }

  if (xml.hasError()) {
    m_lastError = xml.errorString();
    qWarning() << "XML parse error:" << m_lastError;
  }
}

void TacoParser::parseOverlayData(QXmlStreamReader &xml, MarkerPack &pack) {
  while (!xml.atEnd()) {
    xml.readNext();

    if (xml.isEndElement() && xml.name() == QString("OverlayData")) {
      break;
    }

    if (xml.isStartElement()) {
      if (xml.name() == QString("POIs")) {
        // Container for POI and Trail elements (TacO format puts both here)
        while (!xml.atEnd()) {
          xml.readNext();
          if (xml.isEndElement() && xml.name() == QString("POIs"))
            break;
          if (xml.isStartElement()) {
            if (xml.name() == QString("POI")) {
              if (!m_metadataOnly) {
                parsePOI(xml, pack);
              } else {
                xml.skipCurrentElement();
              }
            } else if (xml.name() == QString("Trail")) {
              if (!m_metadataOnly) {
                parseTrail(xml, pack);
              } else {
                xml.skipCurrentElement();
              }
            }
          }
        }
      } else if (xml.name() == QString("POI")) {
        if (!m_metadataOnly) {
          parsePOI(xml, pack);
        } else {
          xml.skipCurrentElement();
        }
      } else if (xml.name() == QString("Trail")) {
        if (!m_metadataOnly) {
          parseTrail(xml, pack);
        } else {
          xml.skipCurrentElement();
        }
      } else if (xml.name() == QString("MarkerCategory")) {
        MarkerCategory cat;
        parseCategory(xml, cat);
        mergeCategory(pack.categories, cat);
      }
    }
  }
}

void TacoParser::parsePOI(QXmlStreamReader &xml, MarkerPack &pack) {
  Marker marker;
  QXmlStreamAttributes attrs = xml.attributes();

  // Required fields
  marker.mapId = attrs.value("MapID").toUInt();
  marker.xpos = attrs.value("xpos").toFloat();
  marker.ypos = attrs.value("ypos").toFloat();
  marker.zpos = attrs.value("zpos").toFloat();

  // GUID
  if (attrs.hasAttribute("GUID")) {
    marker.guid = QUuid(attrs.value("GUID").toString());
  } else {
    marker.guid = QUuid::createUuid();
  }

  // Category type
  marker.type = attrs.value("type").toString();

  // Icon path — resolve to absolute if relative
  QString iconRaw = attrs.value("iconFile").toString();
  if (iconRaw.isEmpty()) {
    iconRaw = attrs.value("texture").toString();
  }
  if (!iconRaw.isEmpty()) {
    marker.iconPath = resolveAssetPath(iconRaw);
  }

  // Appearance
  if (attrs.hasAttribute("iconSize")) {
    marker.iconSize = attrs.value("iconSize").toFloat();
  }
  if (attrs.hasAttribute("alpha")) {
    marker.alpha = attrs.value("alpha").toFloat();
    marker.color.setAlphaF(marker.alpha);
  }
  if (attrs.hasAttribute("color")) {
    marker.color = parseColor(attrs.value("color").toString());
  }
  if (attrs.hasAttribute("minSize")) {
    marker.minSize = attrs.value("minSize").toFloat();
  }
  if (attrs.hasAttribute("maxSize")) {
    marker.maxSize = attrs.value("maxSize").toFloat();
  }
  if (attrs.hasAttribute("heightOffset")) {
    marker.heightOffset = attrs.value("heightOffset").toFloat();
  }

  // Rotation
  if (attrs.hasAttribute("rotate-x")) {
    marker.rotationX = attrs.value("rotate-x").toFloat();
  }
  if (attrs.hasAttribute("rotate-y")) {
    marker.rotationY = attrs.value("rotate-y").toFloat();
  }
  if (attrs.hasAttribute("rotate-z")) {
    marker.rotationZ = attrs.value("rotate-z").toFloat();
  }

  // Visibility
  if (attrs.hasAttribute("fadeNear")) {
    marker.fadeNear = attrs.value("fadeNear").toFloat();
  }
  if (attrs.hasAttribute("fadeFar")) {
    marker.fadeFar = attrs.value("fadeFar").toFloat();
  }

  // Minimap
  if (attrs.hasAttribute("miniMapSize")) {
    marker.miniMapSize = attrs.value("miniMapSize").toInt();
  }
  if (attrs.hasAttribute("miniMapFadeOutLevel")) {
    marker.miniMapFadeOutLevel = attrs.value("miniMapFadeOutLevel").toFloat();
  }
  if (attrs.hasAttribute("miniMapVisibility")) {
    marker.miniMapVisible = attrs.value("miniMapVisibility").toInt() != 0;
  }
  if (attrs.hasAttribute("mapVisibility")) {
    marker.bigMapVisible = attrs.value("mapVisibility").toInt() != 0;
  }
  if (attrs.hasAttribute("inGameVisibility")) {
    marker.inGameVisible = attrs.value("inGameVisibility").toInt() != 0;
  }
  if (attrs.hasAttribute("scaleOnMapWithZoom")) {
    marker.scaleWithZoom = attrs.value("scaleOnMapWithZoom").toInt() != 0;
  }
  if (attrs.hasAttribute("keepOnMapEdge")) {
    marker.keepOnMapEdge = attrs.value("keepOnMapEdge").toInt() != 0;
  }

  // Behavior
  if (attrs.hasAttribute("behavior")) {
    marker.behavior = parseBehavior(attrs.value("behavior").toInt());
  }
  if (attrs.hasAttribute("resetLength")) {
    marker.resetLength = attrs.value("resetLength").toInt();
  }
  if (attrs.hasAttribute("triggerRange")) {
    marker.triggerRange = attrs.value("triggerRange").toFloat();
  }
  if (attrs.hasAttribute("autoTrigger")) {
    marker.autoTrigger = attrs.value("autoTrigger").toInt() != 0;
  }
  if (attrs.hasAttribute("hasCountdown")) {
    marker.hasCountdown = attrs.value("hasCountdown").toInt() != 0;
  }

  // Achievement tracking
  if (attrs.hasAttribute("achievementId")) {
    marker.achievementId = attrs.value("achievementId").toInt();
  }
  if (attrs.hasAttribute("achievementBit")) {
    marker.achievementBit = attrs.value("achievementBit").toInt();
  }

  // Info & actions
  marker.info = attrs.value("info").toString();
  if (attrs.hasAttribute("infoRange")) {
    marker.infoRange = attrs.value("infoRange").toFloat();
  }
  marker.copy = attrs.value("copy").toString();
  marker.copyMessage = attrs.value("copy-message").toString();

  // Toggle category
  marker.toggleCategory = attrs.value("toggleCategory").toString();

  // Filters
  if (attrs.hasAttribute("festival")) {
    marker.festival = static_cast<uint8_t>(attrs.value("festival").toUInt());
  }
  if (attrs.hasAttribute("mount")) {
    marker.mountFilter = attrs.value("mount").toUInt();
  }
  if (attrs.hasAttribute("profession")) {
    marker.profession = attrs.value("profession").toUInt();
  }
  if (attrs.hasAttribute("race")) {
    marker.race = attrs.value("race").toUInt();
  }
  if (attrs.hasAttribute("specialization")) {
    marker.specialization = attrs.value("specialization").toUInt();
  }

  // Behavioral flags
  if (attrs.hasAttribute("canFade")) {
    marker.canFade = attrs.value("canFade").toInt() != 0;
  }
  if (attrs.hasAttribute("invertBehavior")) {
    marker.invertBehavior = attrs.value("invertBehavior").toInt() != 0;
  }

  pack.markers.append(marker);
}

void TacoParser::parseTrail(QXmlStreamReader &xml, MarkerPack &pack) {
  Trail trail;
  QXmlStreamAttributes attrs = xml.attributes();

  trail.type = attrs.value("type").toString();

  // Trail data path — resolve to absolute
  QString trailRaw = attrs.value("trailData").toString();
  if (!trailRaw.isEmpty()) {
    trail.trailDataPath = resolveAssetPath(trailRaw);
  }

  // Trail texture — resolve to absolute
  QString texRaw = attrs.value("texture").toString();
  if (!texRaw.isEmpty()) {
    trail.texturePath = resolveAssetPath(texRaw);
  }

  // Appearance
  if (attrs.hasAttribute("color")) {
    trail.color = parseColor(attrs.value("color").toString());
  }
  if (attrs.hasAttribute("alpha")) {
    trail.alpha = attrs.value("alpha").toFloat();
    trail.color.setAlphaF(trail.alpha);
  }
  if (attrs.hasAttribute("animSpeed")) {
    trail.animSpeed = attrs.value("animSpeed").toFloat();
  }
  if (attrs.hasAttribute("trailScale")) {
    trail.trailScale = attrs.value("trailScale").toFloat();
  }

  // Visibility
  if (attrs.hasAttribute("fadeNear")) {
    trail.fadeNear = attrs.value("fadeNear").toFloat();
  }
  if (attrs.hasAttribute("fadeFar")) {
    trail.fadeFar = attrs.value("fadeFar").toFloat();
  }
  if (attrs.hasAttribute("miniMapVisibility")) {
    trail.miniMapVisible = attrs.value("miniMapVisibility").toInt() != 0;
  }
  if (attrs.hasAttribute("mapVisibility")) {
    trail.bigMapVisible = attrs.value("mapVisibility").toInt() != 0;
  }
  if (attrs.hasAttribute("inGameVisibility")) {
    trail.inGameVisible = attrs.value("inGameVisibility").toInt() != 0;
  }

  // Filters
  if (attrs.hasAttribute("festival")) {
    trail.festival = static_cast<uint8_t>(attrs.value("festival").toUInt());
  }
  if (attrs.hasAttribute("mount")) {
    trail.mountFilter = attrs.value("mount").toUInt();
  }
  if (attrs.hasAttribute("profession")) {
    trail.profession = attrs.value("profession").toUInt();
  }
  if (attrs.hasAttribute("race")) {
    trail.race = attrs.value("race").toUInt();
  }
  if (attrs.hasAttribute("specialization")) {
    trail.specialization = attrs.value("specialization").toUInt();
  }

  // Read trail header only (mapId) — points loaded on demand by MarkerManager
  // when the player enters the corresponding map. Saves ~100s of MB of RAM
  // since most trails are for maps the player isn't on.
  if (!trail.trailDataPath.isEmpty()) {
    parseTrailHeader(trail.trailDataPath, &trail.mapId);
  }

  pack.trails.append(trail);
}

void TacoParser::parseCategory(QXmlStreamReader &xml,
                               MarkerCategory &category) {
  QXmlStreamAttributes attrs = xml.attributes();

  // TacO packs use both "name" (lowercase) and "Name" (capitalized)
  // QXmlStreamReader::value() is case-sensitive, so check both
  category.name = attrs.value("name").toString();
  if (category.name.isEmpty()) {
    category.name = attrs.value("Name").toString();
  }
  category.displayName = attrs.value("DisplayName").toString();
  if (category.displayName.isEmpty()) {
    category.displayName = category.name;
  }

  // Set fullName from name if not already set (root categories)
  // This ensures children can build their fullName during recursive parsing
  if (category.fullName.isEmpty()) {
    category.fullName = category.name;
  }

  // Icon path — resolve to absolute
  QString iconRaw = attrs.value("iconFile").toString();
  if (!iconRaw.isEmpty()) {
    category.iconPath = resolveAssetPath(iconRaw);
    category.fieldsSet |= MarkerCategory::FieldIconPath;
  }

  category.defaultToggle = attrs.value("defaultToggle").toString() != "0";

  // Section separator (TacO uses these for visual dividers like "[-CORE
  // GAME-]")
  if (attrs.hasAttribute("IsSeparator")) {
    category.isSeparator = attrs.value("IsSeparator").toInt() != 0;
  }

  // --- Parse inheritable attributes (track which were explicitly set) ---
  if (attrs.hasAttribute("iconSize")) {
    category.iconSize = attrs.value("iconSize").toFloat();
    category.fieldsSet |= MarkerCategory::FieldIconSize;
  }
  if (attrs.hasAttribute("alpha")) {
    category.alpha = attrs.value("alpha").toFloat();
    category.fieldsSet |= MarkerCategory::FieldAlpha;
  }
  if (attrs.hasAttribute("color")) {
    category.color = parseColor(attrs.value("color").toString());
    category.fieldsSet |= MarkerCategory::FieldColor;
  }
  if (attrs.hasAttribute("minSize")) {
    category.minSize = attrs.value("minSize").toFloat();
    category.fieldsSet |= MarkerCategory::FieldMinSize;
  }
  if (attrs.hasAttribute("maxSize")) {
    category.maxSize = attrs.value("maxSize").toFloat();
    category.fieldsSet |= MarkerCategory::FieldMaxSize;
  }
  if (attrs.hasAttribute("heightOffset")) {
    category.heightOffset = attrs.value("heightOffset").toFloat();
    category.fieldsSet |= MarkerCategory::FieldHeightOffset;
  }
  if (attrs.hasAttribute("fadeNear")) {
    category.fadeNear = attrs.value("fadeNear").toFloat();
    category.fieldsSet |= MarkerCategory::FieldFadeNear;
  }
  if (attrs.hasAttribute("fadeFar")) {
    category.fadeFar = attrs.value("fadeFar").toFloat();
    category.fieldsSet |= MarkerCategory::FieldFadeFar;
  }
  if (attrs.hasAttribute("animSpeed")) {
    category.animSpeed = attrs.value("animSpeed").toFloat();
    category.fieldsSet |= MarkerCategory::FieldAnimSpeed;
  }
  if (attrs.hasAttribute("trailScale")) {
    category.trailScale = attrs.value("trailScale").toFloat();
    category.fieldsSet |= MarkerCategory::FieldTrailScale;
  }
  if (attrs.hasAttribute("triggerRange")) {
    category.triggerRange = attrs.value("triggerRange").toFloat();
    category.fieldsSet |= MarkerCategory::FieldTriggerRange;
  }
  if (attrs.hasAttribute("miniMapSize")) {
    category.miniMapSize = attrs.value("miniMapSize").toInt();
    category.fieldsSet |= MarkerCategory::FieldMiniMapSize;
  }
  if (attrs.hasAttribute("miniMapFadeOutLevel")) {
    category.miniMapFadeOutLevel = attrs.value("miniMapFadeOutLevel").toFloat();
    category.fieldsSet |= MarkerCategory::FieldMiniMapFade;
  }
  if (attrs.hasAttribute("miniMapVisibility")) {
    category.miniMapVisible = attrs.value("miniMapVisibility").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldMiniMapVisible;
  }
  if (attrs.hasAttribute("mapVisibility")) {
    category.bigMapVisible = attrs.value("mapVisibility").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldBigMapVisible;
  }
  if (attrs.hasAttribute("inGameVisibility")) {
    category.inGameVisible = attrs.value("inGameVisibility").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldInGameVisible;
  }
  if (attrs.hasAttribute("scaleOnMapWithZoom")) {
    category.scaleWithZoom = attrs.value("scaleOnMapWithZoom").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldScaleWithZoom;
  }
  if (attrs.hasAttribute("keepOnMapEdge")) {
    category.keepOnMapEdge = attrs.value("keepOnMapEdge").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldKeepOnMapEdge;
  }
  if (attrs.hasAttribute("autoTrigger")) {
    category.autoTrigger = attrs.value("autoTrigger").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldAutoTrigger;
  }
  if (attrs.hasAttribute("hasCountdown")) {
    category.hasCountdown = attrs.value("hasCountdown").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldHasCountdown;
  }
  if (attrs.hasAttribute("canFade")) {
    category.canFade = attrs.value("canFade").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldCanFade;
  }
  if (attrs.hasAttribute("invertBehavior")) {
    category.invertBehavior = attrs.value("invertBehavior").toInt() != 0;
    category.fieldsSet |= MarkerCategory::FieldInvertBehavior;
  }
  if (attrs.hasAttribute("behavior")) {
    category.behavior = parseBehavior(attrs.value("behavior").toInt());
    category.fieldsSet |= MarkerCategory::FieldBehavior;
  }
  if (attrs.hasAttribute("resetLength")) {
    category.resetLength = attrs.value("resetLength").toInt();
    category.fieldsSet |= MarkerCategory::FieldResetLength;
  }
  QString texRaw = attrs.value("texture").toString();
  if (!texRaw.isEmpty()) {
    category.texturePath = resolveAssetPath(texRaw);
    category.fieldsSet |= MarkerCategory::FieldTexturePath;
  }
  QString toggleRaw = attrs.value("toggleCategory").toString();
  if (!toggleRaw.isEmpty()) {
    category.toggleCategory = toggleRaw;
    category.fieldsSet |= MarkerCategory::FieldToggleCategory;
  }
  if (attrs.hasAttribute("festival")) {
    category.festival = static_cast<uint8_t>(attrs.value("festival").toUInt());
    category.fieldsSet |= MarkerCategory::FieldFestival;
  }
  if (attrs.hasAttribute("mount")) {
    category.mountFilter = attrs.value("mount").toUInt();
    category.fieldsSet |= MarkerCategory::FieldMountFilter;
  }
  if (attrs.hasAttribute("profession")) {
    category.profession = attrs.value("profession").toUInt();
    category.fieldsSet |= MarkerCategory::FieldProfession;
  }
  if (attrs.hasAttribute("race")) {
    category.race = attrs.value("race").toUInt();
    category.fieldsSet |= MarkerCategory::FieldRace;
  }
  if (attrs.hasAttribute("specialization")) {
    category.specialization = attrs.value("specialization").toUInt();
    category.fieldsSet |= MarkerCategory::FieldSpecialization;
  }

  // Parse child categories
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isEndElement() && xml.name() == QString("MarkerCategory")) {
      break;
    }
    if (xml.isStartElement() && xml.name() == QString("MarkerCategory")) {
      MarkerCategory child;
      // Pre-read name so we can build fullName BEFORE recursive parse
      QString childName = xml.attributes().value("name").toString();
      if (childName.isEmpty()) {
        childName = xml.attributes().value("Name").toString();
      }
      child.fullName = category.fullName + "." + childName;
      parseCategory(xml, child);
      category.children.append(child);
    }
  }
}

bool TacoParser::parseTrailHeader(const QString &trlPath, uint32_t *outMapId) {
  QFile file(trlPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  QDataStream stream(&file);
  stream.setByteOrder(QDataStream::LittleEndian);

  // TRL header: [uint32 version] [uint32 mapId]
  uint32_t version;
  uint32_t mapId;
  stream >> version >> mapId;

  if (stream.status() != QDataStream::Ok) {
    return false;
  }

  if (outMapId) {
    *outMapId = mapId;
  }
  return true;
}

QList<QVector3D> TacoParser::parseTrailFile(const QString &trlPath,
                                            uint32_t *outMapId) {
  QList<QVector3D> points;

  QFile file(trlPath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open trail file:" << trlPath;
    return points;
  }

  QDataStream stream(&file);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);

  // TRL binary format (8-byte header):
  //   [uint32 version]   — always 0 in known packs
  //   [uint32 mapId]     — GW2 map ID for this trail
  //   [float3 * N]       — trail waypoints (x, y, z)
  //
  // Binary inspection confirmed: first uint32 is always 0 (version/padding),
  // second uint32 is the actual map ID (e.g., 873, 32, 39, 138).
  // With 8-byte header, (fileSize - 8) / 12 gives clean integer point count.
  uint32_t version;
  uint32_t mapId;
  stream >> version >> mapId;

  if (outMapId) {
    *outMapId = mapId;
  }

  while (!stream.atEnd()) {
    float x, y, z;
    stream >> x >> y >> z;
    if (stream.status() == QDataStream::Ok) {
      points.append(QVector3D(x, y, z));
    }
  }

  file.close();
  return points;
}

QString TacoParser::resolveAssetPath(const QString &relativePath) const {
  if (m_basePath.isEmpty()) {
    return relativePath;
  }

  // Normalize path separators (TacO uses backslashes)
  QString normalized = relativePath;
  normalized.replace('\\', '/');

  QDir baseDir(m_basePath);
  QString absolute = baseDir.filePath(normalized);

  // Check if file exists
  if (QFileInfo::exists(absolute)) {
    return absolute;
  }

  // Try case-insensitive search (TacO packs often have mismatched case)
  QStringList parts = normalized.split('/');
  QString current = m_basePath;

  for (const QString &part : parts) {
    QDir dir(current);
    QStringList entries =
        dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);

    bool found = false;
    for (const QString &entry : entries) {
      if (entry.compare(part, Qt::CaseInsensitive) == 0) {
        current = dir.filePath(entry);
        found = true;
        break;
      }
    }

    if (!found) {
      // File not found even with case-insensitive search
      return absolute; // Return the normalized path anyway, let caller handle
    }
  }

  return current;
}

QColor TacoParser::parseColor(const QString &colorStr) {
  // Format: AARRGGBB or RRGGBB
  bool ok;
  uint32_t val = colorStr.toUInt(&ok, 16);

  if (!ok)
    return Qt::white;

  if (colorStr.length() <= 6) {
    // RRGGBB
    return QColor((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
  } else {
    // AARRGGBB
    return QColor((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF,
                  (val >> 24) & 0xFF);
  }
}

MarkerBehavior TacoParser::parseBehavior(int value) {
  if (value >= 0 && value <= 9) {
    return static_cast<MarkerBehavior>(value);
  }
  return MarkerBehavior::AlwaysVisible;
}

// ---------------------------------------------------------------------------
// Category inheritance (TacO pattern)
// ---------------------------------------------------------------------------

void TacoParser::buildCategoryMap(QList<MarkerCategory> &categories,
                                  QHash<QString, MarkerCategory *> &map) {
  for (int i = 0; i < categories.size(); ++i) {
    MarkerCategory &cat = categories[i];
    if (!cat.fullName.isEmpty()) {
      map.insert(cat.fullName.toLower(), &cat);
    }
    // Recurse into children
    buildCategoryMap(cat.children, map);
  }
}

void TacoParser::mergeCategory(QList<MarkerCategory> &existing,
                               const MarkerCategory &incoming) {
  // Look for an existing category with the same name (case-insensitive,
  // matching TacO's ToLower() behavior in RecursiveImportPOIType)
  for (int i = 0; i < existing.size(); ++i) {
    if (existing[i].name.compare(incoming.name, Qt::CaseInsensitive) == 0) {
      MarkerCategory &target = existing[i];

      // Update displayName if incoming has a meaningful one (last-write-wins,
      // matching TacO which overwrites displayName on every encounter)
      if (!incoming.displayName.isEmpty() &&
          incoming.displayName != incoming.name) {
        target.displayName = incoming.displayName;
      }

      // OR-merge the fieldsSet bitmask — adopt any fields the incoming
      // category explicitly set that the existing one didn't
      uint64_t newFields = incoming.fieldsSet & ~target.fieldsSet;
      if (newFields) {
        using FB = MarkerCategory::FieldBit;
#define MERGE_FIELD(bit, field)                                                \
  if (newFields & FB::bit) {                                                   \
    target.field = incoming.field;                                             \
  }
        MERGE_FIELD(FieldIconPath, iconPath)
        MERGE_FIELD(FieldColor, color)
        MERGE_FIELD(FieldIconSize, iconSize)
        MERGE_FIELD(FieldAlpha, alpha)
        MERGE_FIELD(FieldMinSize, minSize)
        MERGE_FIELD(FieldMaxSize, maxSize)
        MERGE_FIELD(FieldHeightOffset, heightOffset)
        MERGE_FIELD(FieldFadeNear, fadeNear)
        MERGE_FIELD(FieldFadeFar, fadeFar)
        MERGE_FIELD(FieldAnimSpeed, animSpeed)
        MERGE_FIELD(FieldTrailScale, trailScale)
        MERGE_FIELD(FieldTriggerRange, triggerRange)
        MERGE_FIELD(FieldMiniMapSize, miniMapSize)
        MERGE_FIELD(FieldMiniMapFade, miniMapFadeOutLevel)
        MERGE_FIELD(FieldMiniMapVisible, miniMapVisible)
        MERGE_FIELD(FieldBigMapVisible, bigMapVisible)
        MERGE_FIELD(FieldInGameVisible, inGameVisible)
        MERGE_FIELD(FieldScaleWithZoom, scaleWithZoom)
        MERGE_FIELD(FieldKeepOnMapEdge, keepOnMapEdge)
        MERGE_FIELD(FieldAutoTrigger, autoTrigger)
        MERGE_FIELD(FieldHasCountdown, hasCountdown)
        MERGE_FIELD(FieldCanFade, canFade)
        MERGE_FIELD(FieldInvertBehavior, invertBehavior)
        MERGE_FIELD(FieldBehavior, behavior)
        MERGE_FIELD(FieldResetLength, resetLength)
        MERGE_FIELD(FieldTexturePath, texturePath)
        MERGE_FIELD(FieldToggleCategory, toggleCategory)
        MERGE_FIELD(FieldFestival, festival)
        MERGE_FIELD(FieldMountFilter, mountFilter)
        MERGE_FIELD(FieldProfession, profession)
        MERGE_FIELD(FieldRace, race)
        MERGE_FIELD(FieldSpecialization, specialization)
#undef MERGE_FIELD
        target.fieldsSet |= newFields;
      }

      // Recursively merge children
      for (const MarkerCategory &child : incoming.children) {
        mergeCategory(target.children, child);
      }
      return;
    }
  }

  // No match found — append as new category
  existing.append(incoming);
}

void TacoParser::propagateCategoryInheritance(MarkerCategory &parent,
                                              MarkerCategory &child) {
  // For each field: if child did NOT explicitly set it, inherit from parent
  using FB = MarkerCategory::FieldBit;

  // Helper macro: if child doesn't have the bit set but parent does, copy
#define INHERIT_FIELD(bit, field)                                              \
  if (!(child.fieldsSet & FB::bit) && (parent.fieldsSet & FB::bit)) {          \
    child.field = parent.field;                                                \
    child.fieldsSet |= FB::bit;                                                \
  }

  INHERIT_FIELD(FieldIconPath, iconPath)
  INHERIT_FIELD(FieldColor, color)
  INHERIT_FIELD(FieldIconSize, iconSize)
  INHERIT_FIELD(FieldAlpha, alpha)
  INHERIT_FIELD(FieldMinSize, minSize)
  INHERIT_FIELD(FieldMaxSize, maxSize)
  INHERIT_FIELD(FieldHeightOffset, heightOffset)
  INHERIT_FIELD(FieldFadeNear, fadeNear)
  INHERIT_FIELD(FieldFadeFar, fadeFar)
  INHERIT_FIELD(FieldAnimSpeed, animSpeed)
  INHERIT_FIELD(FieldTrailScale, trailScale)
  INHERIT_FIELD(FieldTriggerRange, triggerRange)
  INHERIT_FIELD(FieldMiniMapSize, miniMapSize)
  INHERIT_FIELD(FieldMiniMapFade, miniMapFadeOutLevel)
  INHERIT_FIELD(FieldMiniMapVisible, miniMapVisible)
  INHERIT_FIELD(FieldBigMapVisible, bigMapVisible)
  INHERIT_FIELD(FieldInGameVisible, inGameVisible)
  INHERIT_FIELD(FieldScaleWithZoom, scaleWithZoom)
  INHERIT_FIELD(FieldKeepOnMapEdge, keepOnMapEdge)
  INHERIT_FIELD(FieldAutoTrigger, autoTrigger)
  INHERIT_FIELD(FieldHasCountdown, hasCountdown)
  INHERIT_FIELD(FieldCanFade, canFade)
  INHERIT_FIELD(FieldInvertBehavior, invertBehavior)
  INHERIT_FIELD(FieldBehavior, behavior)
  INHERIT_FIELD(FieldResetLength, resetLength)
  INHERIT_FIELD(FieldTexturePath, texturePath)
  INHERIT_FIELD(FieldToggleCategory, toggleCategory)
  INHERIT_FIELD(FieldFestival, festival)
  INHERIT_FIELD(FieldMountFilter, mountFilter)
  INHERIT_FIELD(FieldProfession, profession)
  INHERIT_FIELD(FieldRace, race)
  INHERIT_FIELD(FieldSpecialization, specialization)

#undef INHERIT_FIELD

  // Recurse: propagate this child's (now-merged) data to its own children
  for (int i = 0; i < child.children.size(); ++i) {
    propagateCategoryInheritance(child, child.children[i]);
  }
}

void TacoParser::resolveMarkerInheritance(Marker &marker,
                                          const MarkerCategory &category) {
  using FB = MarkerCategory::FieldBit;

  // Inherit icon if marker has none
  if (marker.iconPath.isEmpty() && (category.fieldsSet & FB::FieldIconPath)) {
    marker.iconPath = category.iconPath;
  }

  // Inherit appearance fields if marker uses defaults
  // Note: POIs don't have "fieldsSet" tracking — we check for default values
  // This is acceptable because TacO uses the same approach: default = inherit

#define INHERIT_IF_DEFAULT_F(catBit, mField, catField, defaultVal)             \
  if ((category.fieldsSet & FB::catBit) &&                                     \
      (marker.mField == static_cast<decltype(marker.mField)>(defaultVal))) {   \
    marker.mField = category.catField;                                         \
  }

#define INHERIT_IF_DEFAULT_B(catBit, mField, catField, defaultVal)             \
  if ((category.fieldsSet & FB::catBit) && (marker.mField == defaultVal)) {    \
    marker.mField = category.catField;                                         \
  }

  INHERIT_IF_DEFAULT_F(FieldIconSize, iconSize, iconSize, 1.0f)
  INHERIT_IF_DEFAULT_F(FieldAlpha, alpha, alpha, 1.0f)
  INHERIT_IF_DEFAULT_F(FieldMinSize, minSize, minSize, 5.0f)
  INHERIT_IF_DEFAULT_F(FieldMaxSize, maxSize, maxSize, 2048.0f)
  INHERIT_IF_DEFAULT_F(FieldHeightOffset, heightOffset, heightOffset, 1.5f)
  INHERIT_IF_DEFAULT_F(FieldFadeNear, fadeNear, fadeNear, -1.0f)
  INHERIT_IF_DEFAULT_F(FieldFadeFar, fadeFar, fadeFar, -1.0f)
  INHERIT_IF_DEFAULT_F(FieldTriggerRange, triggerRange, triggerRange, 2.0f)
  INHERIT_IF_DEFAULT_F(FieldMiniMapSize, miniMapSize, miniMapSize, 20)
  INHERIT_IF_DEFAULT_F(FieldMiniMapFade, miniMapFadeOutLevel,
                       miniMapFadeOutLevel, 100.0f)

  // Bool fields: inherit only if category explicitly set them
  if (category.fieldsSet & FB::FieldBehavior) {
    if (marker.behavior == MarkerBehavior::AlwaysVisible) {
      marker.behavior = category.behavior;
    }
  }
  if (category.fieldsSet & FB::FieldResetLength) {
    if (marker.resetLength == 0) {
      marker.resetLength = category.resetLength;
    }
  }

  // Color: inherit if marker is default white
  if ((category.fieldsSet & FB::FieldColor) &&
      marker.color == QColor(255, 255, 255, 255)) {
    marker.color = category.color;
  }

  // Filters: inherit if marker has 0 (no filter)
  INHERIT_IF_DEFAULT_F(FieldFestival, festival, festival, 0)
  INHERIT_IF_DEFAULT_F(FieldMountFilter, mountFilter, mountFilter, 0)
  INHERIT_IF_DEFAULT_F(FieldProfession, profession, profession, 0)
  INHERIT_IF_DEFAULT_F(FieldRace, race, race, 0)
  INHERIT_IF_DEFAULT_F(FieldSpecialization, specialization, specialization, 0)

  // Toggle category: inherit if marker has none
  if (marker.toggleCategory.isEmpty() &&
      (category.fieldsSet & FB::FieldToggleCategory)) {
    marker.toggleCategory = category.toggleCategory;
  }

  // Visibility flags: categories can mark items as map-only or minimap-only.
  // Inherit unconditionally when category explicitly sets them, because
  // markers don't track their own fieldsSet and the default (true) is
  // indistinguishable from an explicit true. If the marker's own XML
  // attribute overrides, that happened during parsePOI() before this runs.
  if (category.fieldsSet & FB::FieldInGameVisible) {
    marker.inGameVisible = category.inGameVisible;
  }
  if (category.fieldsSet & FB::FieldMiniMapVisible) {
    marker.miniMapVisible = category.miniMapVisible;
  }
  if (category.fieldsSet & FB::FieldBigMapVisible) {
    marker.bigMapVisible = category.bigMapVisible;
  }

#undef INHERIT_IF_DEFAULT_F
#undef INHERIT_IF_DEFAULT_B
}

void TacoParser::resolveTrailInheritance(Trail &trail,
                                         const MarkerCategory &category) {
  using FB = MarkerCategory::FieldBit;

  // Inherit texture if trail has none
  if (trail.texturePath.isEmpty() &&
      (category.fieldsSet & FB::FieldTexturePath)) {
    trail.texturePath = category.texturePath;
  }

  // Inherit appearance
  if ((category.fieldsSet & FB::FieldAnimSpeed) && trail.animSpeed == 1.0f) {
    trail.animSpeed = category.animSpeed;
  }
  if ((category.fieldsSet & FB::FieldTrailScale) && trail.trailScale == 1.0f) {
    trail.trailScale = category.trailScale;
  }
  if ((category.fieldsSet & FB::FieldAlpha) && trail.alpha == 1.0f) {
    trail.alpha = category.alpha;
  }
  if ((category.fieldsSet & FB::FieldColor) &&
      trail.color == QColor(255, 255, 255, 255)) {
    trail.color = category.color;
  }
  if ((category.fieldsSet & FB::FieldFadeNear) && trail.fadeNear == -1.0f) {
    trail.fadeNear = category.fadeNear;
  }
  if ((category.fieldsSet & FB::FieldFadeFar) && trail.fadeFar == -1.0f) {
    trail.fadeFar = category.fadeFar;
  }

  // Inherit filters (0 = show for all — only inherit if trail didn't set its
  // own)
  if ((category.fieldsSet & FB::FieldFestival) && trail.festival == 0) {
    trail.festival = category.festival;
  }
  if ((category.fieldsSet & FB::FieldMountFilter) && trail.mountFilter == 0) {
    trail.mountFilter = category.mountFilter;
  }
  if ((category.fieldsSet & FB::FieldProfession) && trail.profession == 0) {
    trail.profession = category.profession;
  }
  if ((category.fieldsSet & FB::FieldRace) && trail.race == 0) {
    trail.race = category.race;
  }
  if ((category.fieldsSet & FB::FieldSpecialization) &&
      trail.specialization == 0) {
    trail.specialization = category.specialization;
  }

  // Visibility flags: categories can mark trails as map-only or 3D-only.
  // See resolveMarkerInheritance for rationale on unconditional inheritance.
  if (category.fieldsSet & FB::FieldInGameVisible) {
    trail.inGameVisible = category.inGameVisible;
  }
  if (category.fieldsSet & FB::FieldMiniMapVisible) {
    trail.miniMapVisible = category.miniMapVisible;
  }
  if (category.fieldsSet & FB::FieldBigMapVisible) {
    trail.bigMapVisible = category.bigMapVisible;
  }
}

void TacoParser::applyInheritance(MarkerPack &pack) {
  // Step 1: Propagate category data top-down (parent → children)
  for (int i = 0; i < pack.categories.size(); ++i) {
    MarkerCategory &root = pack.categories[i];
    // Set fullName for root categories if not already set
    if (root.fullName.isEmpty()) {
      root.fullName = root.name;
    }
    for (int j = 0; j < root.children.size(); ++j) {
      propagateCategoryInheritance(root, root.children[j]);
    }
  }

  // Step 2: Build flat lookup map (fullName → MarkerCategory*)
  QHash<QString, MarkerCategory *> catMap;
  buildCategoryMap(pack.categories, catMap);

  // Step 3: Resolve each marker's unset fields from its category
  for (int i = 0; i < pack.markers.size(); ++i) {
    Marker &marker = pack.markers[i];
    if (!marker.type.isEmpty()) {
      QString key = marker.type.toLower();
      auto it = catMap.find(key);
      if (it != catMap.end()) {
        resolveMarkerInheritance(marker, *it.value());
      }
    }
  }

  // Step 4: Resolve each trail's unset fields from its category
  for (int i = 0; i < pack.trails.size(); ++i) {
    Trail &trail = pack.trails[i];
    if (!trail.type.isEmpty()) {
      QString key = trail.type.toLower();
      auto it = catMap.find(key);
      if (it != catMap.end()) {
        resolveTrailInheritance(trail, *it.value());
      }
    }
  }

  qInfo() << "Inheritance resolved:" << catMap.size() << "categories mapped";
}

// ---------------------------------------------------------------------------
// AIO pack.json metadata
// ---------------------------------------------------------------------------
void TacoParser::parsePackJson(const QString &path, MarkerPack &pack) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Failed to open pack.json:" << path;
    return;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "Invalid pack.json:" << parseError.errorString();
    return;
  }

  if (!doc.isObject()) {
    qWarning() << "pack.json is not a JSON object";
    return;
  }

  QJsonObject obj = doc.object();

  // Validate type and version (versioned file format per dev standards)
  QString type = obj.value("type").toString();
  int version = obj.value("version").toInt(0);

  if (type != "aio-marker-pack") {
    qWarning() << "pack.json has unrecognized type:" << type;
    return;
  }

  if (version < 1) {
    qWarning() << "pack.json has unsupported version:" << version;
    return;
  }

  // Extract metadata (all fields optional except type/version)
  QString jsonName = obj.value("name").toString();
  if (!jsonName.isEmpty()) {
    pack.name = jsonName; // Override filename-derived name
  }

  pack.author = obj.value("author").toString();
  pack.description = obj.value("description").toString();
  pack.website = obj.value("website").toString();

  QString jsonVersion = obj.value("pack_version").toString();
  if (!jsonVersion.isEmpty()) {
    pack.version = jsonVersion;
  }

  qInfo() << "Loaded pack.json metadata for" << pack.name
          << "author:" << pack.author;
}

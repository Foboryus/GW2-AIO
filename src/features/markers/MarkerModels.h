#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QUuid>
#include <QVector3D>
#include <cstdint>

/**
 * @brief Marker behavior types (matches TacO specification exactly)
 * Reference: TacO gw2tactical.h POIBehavior enum
 */
enum class MarkerBehavior {
  AlwaysVisible = 0,
  ReappearOnMapChange = 1,
  ReappearOnDailyReset = 2,
  OnlyVisibleBeforeActivation = 3,
  ReappearAfterTimer = 4,
  ReappearOnMapReset = 5,
  OncePerInstance = 6,
  DailyPerChar = 7,
  OncePerInstancePerChar = 8,
  WvWObjective = 9
};

/**
 * @brief Single point of interest marker (TacO POI)
 *
 * Field names match TacO's XML attribute names where possible.
 * All defaults match TacO's MarkerTypeData defaults.
 */
struct Marker {
  // Identity
  QUuid guid;
  QString type; // Category path (e.g., "harvest.ore.iron")

  // Position
  uint32_t mapId = 0;
  float xpos = 0;
  float ypos = 0;
  float zpos = 0;
  float rotationX = 0;
  float rotationY = 0;
  float rotationZ = 0;

  // Appearance
  QString iconPath; // Resolved path to texture (absolute or ZIP-relative)
  QColor color{255, 255, 255, 255};
  float iconSize = 1.0f;
  float alpha = 1.0f;
  float minSize = 5.0f;
  float maxSize = 2048.0f;
  float heightOffset = 1.5f;

  // Visibility
  float fadeNear = -1.0f; // -1 = no fading (TacO default)
  float fadeFar = -1.0f;

  // Minimap
  int miniMapSize = 20;
  float miniMapFadeOutLevel = 100.0f;
  bool miniMapVisible = true;
  bool bigMapVisible = true;
  bool inGameVisible = true;
  bool scaleWithZoom = false;
  bool keepOnMapEdge = false;

  // Behavior
  MarkerBehavior behavior = MarkerBehavior::AlwaysVisible;
  int resetLength = 0;
  float triggerRange = 2.0f;
  bool autoTrigger = false;
  bool hasCountdown = false;

  // Achievement tracking
  int achievementId = -1;
  int achievementBit = -1;

  // Info & actions
  QString info;
  float infoRange = 2.0f;
  QString copy;        // Text to copy to clipboard on trigger
  QString copyMessage; // Message to show on copy

  // Toggle category (TacO toggleCategory attribute)
  QString toggleCategory;

  // Filters (0 = show for all, non-zero = bitmask match required)
  uint8_t festival = 0;        // Festival bitmask (SAB, Wintersday, etc.)
  uint32_t mountFilter = 0;    // Mount bitmask
  uint32_t profession = 0;     // Profession bitmask
  uint32_t race = 0;           // Race bitmask
  uint32_t specialization = 0; // Specialization bitmask

  // Behavioral flags
  bool canFade = true;         // Whether fade near/far applies
  bool invertBehavior = false; // Invert behavior visibility logic

  // Runtime state (NOT persisted — managed by MarkerManager)
  bool visible = true;
  bool activated = false;

  QVector3D position() const { return QVector3D(xpos, ypos, zpos); }
};

/**
 * @brief Trail path (TacO Trail)
 */
struct Trail {
  // Identity
  QUuid guid;
  QString type; // Category path

  // Data sources
  QString trailDataPath; // Path to .trl file
  QString texturePath;   // Trail texture

  // Map
  uint32_t mapId = 0;

  // Appearance
  QColor color{255, 255, 255, 255};
  float alpha = 1.0f;
  float animSpeed = 1.0f;
  float trailScale = 1.0f;

  // Visibility
  float fadeNear = -1.0f;
  float fadeFar = -1.0f;
  bool miniMapVisible = true;
  bool bigMapVisible = true;
  bool inGameVisible = true;

  // Filters (0 = show for all, non-zero = bitmask match required)
  uint8_t festival = 0;        // Festival bitmask
  uint32_t mountFilter = 0;    // Mount bitmask
  uint32_t profession = 0;     // Profession bitmask
  uint32_t race = 0;           // Race bitmask
  uint32_t specialization = 0; // Specialization bitmask

  // Trail points (loaded from .trl binary)
  QList<QVector3D> points;

  // Runtime
  bool visible = true;
};

/**
 * @brief Category for organizing markers (hierarchical tree)
 *
 * Categories carry inheritable MarkerTypeData. When a POI doesn't
 * set a field, it inherits from its parent category chain (TacO pattern).
 * The 'fieldsSet' bitmask tracks which fields were explicitly set.
 */
struct MarkerCategory {
  QString name;        // Internal name (e.g., "ore")
  QString fullName;    // Full path (e.g., "harvest.ore")
  QString displayName; // User-visible name
  QString iconPath;
  bool defaultToggle = true;
  bool isSeparator = false; // Section header (e.g., "[-CORE GAME-]")

  // Inheritable appearance data (same fields as Marker)
  QColor color{255, 255, 255, 255};
  float iconSize = 1.0f;
  float alpha = 1.0f;
  float minSize = 5.0f;
  float maxSize = 2048.0f;
  float heightOffset = 1.5f;
  float fadeNear = -1.0f;
  float fadeFar = -1.0f;
  float animSpeed = 1.0f;
  float trailScale = 1.0f;
  float triggerRange = 2.0f;
  int miniMapSize = 20;
  float miniMapFadeOutLevel = 100.0f;
  bool miniMapVisible = true;
  bool bigMapVisible = true;
  bool inGameVisible = true;
  bool scaleWithZoom = false;
  bool keepOnMapEdge = false;
  bool autoTrigger = false;
  bool hasCountdown = false;
  bool canFade = true;
  bool invertBehavior = false;
  MarkerBehavior behavior = MarkerBehavior::AlwaysVisible;
  int resetLength = 0;
  QString texturePath; // Trail texture (inheritable)
  QString toggleCategory;

  // Filters (inheritable)
  uint8_t festival = 0;
  uint32_t mountFilter = 0;
  uint32_t profession = 0;
  uint32_t race = 0;
  uint32_t specialization = 0;

  // Bitmask tracking which fields were explicitly set in XML
  // Used by category inheritance: only inherit fields NOT set on the child
  uint64_t fieldsSet = 0;

  // Field bit positions for fieldsSet
  enum FieldBit : uint64_t {
    FieldIconPath = 1ULL << 0,
    FieldColor = 1ULL << 1,
    FieldIconSize = 1ULL << 2,
    FieldAlpha = 1ULL << 3,
    FieldMinSize = 1ULL << 4,
    FieldMaxSize = 1ULL << 5,
    FieldHeightOffset = 1ULL << 6,
    FieldFadeNear = 1ULL << 7,
    FieldFadeFar = 1ULL << 8,
    FieldAnimSpeed = 1ULL << 9,
    FieldTrailScale = 1ULL << 10,
    FieldTriggerRange = 1ULL << 11,
    FieldMiniMapSize = 1ULL << 12,
    FieldMiniMapFade = 1ULL << 13,
    FieldMiniMapVisible = 1ULL << 14,
    FieldBigMapVisible = 1ULL << 15,
    FieldInGameVisible = 1ULL << 16,
    FieldScaleWithZoom = 1ULL << 17,
    FieldKeepOnMapEdge = 1ULL << 18,
    FieldAutoTrigger = 1ULL << 19,
    FieldHasCountdown = 1ULL << 20,
    FieldCanFade = 1ULL << 21,
    FieldInvertBehavior = 1ULL << 22,
    FieldBehavior = 1ULL << 23,
    FieldResetLength = 1ULL << 24,
    FieldTexturePath = 1ULL << 25,
    FieldToggleCategory = 1ULL << 26,
    FieldFestival = 1ULL << 27,
    FieldMountFilter = 1ULL << 28,
    FieldProfession = 1ULL << 29,
    FieldRace = 1ULL << 30,
    FieldSpecialization = 1ULL << 31,
  };

  QList<MarkerCategory> children;

  // Runtime
  bool visible = true;
};

/**
 * @brief A complete marker pack (.taco file or directory)
 */
struct MarkerPack {
  QString id;   // Stable identifier (folder name, not overridable by pack.json)
  QString name; // Display name (may be overridden by pack.json)
  QString path; // Path to .taco or folder
  QString version;

  // Optional metadata (populated from AIO pack.json if present)
  QString author;
  QString description;
  QString website;

  QList<MarkerCategory> categories;
  QList<Marker> markers;
  QList<Trail> trails;

  int markerCount() const { return markers.size(); }
  int trailCount() const { return trails.size(); }
};

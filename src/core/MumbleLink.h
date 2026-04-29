#pragma once

#include <QMatrix4x4>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector3D>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief GW2 Mumble Link API context structure
 * Official API: https://wiki.guildwars2.com/wiki/API:MumbleLink
 */
struct MumbleContext {
  unsigned char serverAddress[28];
  uint32_t mapId;
  uint32_t mapType;
  uint32_t shardId;
  uint32_t instance;
  uint32_t buildId;
  uint32_t uiState;
  uint16_t compassWidth;
  uint16_t compassHeight;
  float compassRotation;
  float playerX;
  float playerY;
  float mapCenterX;
  float mapCenterY;
  float mapScale;
  uint32_t processId;
  uint8_t mountIndex;
};

/**
 * @brief Mumble Link shared memory layout (standardized Mumble API)
 */
struct LinkedMem {
  uint32_t uiVersion;
  uint32_t uiTick;
  float fAvatarPosition[3];
  float fAvatarFront[3];
  float fAvatarTop[3];
  wchar_t name[256];
  float fCameraPosition[3];
  float fCameraFront[3];
  float fCameraTop[3];
  wchar_t identity[256];
  uint32_t context_len;
  unsigned char context[256];
  wchar_t description[2048];
};

/**
 * @brief Compass/minimap data extracted from MumbleContext
 *
 * Used for rendering markers on the minimap and big map.
 * TacO uses BuildTransformationMatrix() to map world → minimap pixel coords.
 */
struct CompassData {
  int compassWidth = 0;
  int compassHeight = 0;
  float compassRotation = 0.0f;
  float playerX = 0.0f;
  float playerY = 0.0f;
  float mapCenterX = 0.0f;
  float mapCenterY = 0.0f;
  float mapScale = 0.0f;
  float uiScale = 1.0f; // UI size scaling factor (TacO applies in matrix step 7)

  /**
   * @brief Build a transformation matrix for minimap/bigmap rendering
   * Converts world coordinates → pixel coordinates within the minimap rect.
   * @param miniRect The screen rectangle of the minimap
   * @param ignoreRotation true for big map (no rotation), false for minimap
   */
  QMatrix4x4 buildTransformationMatrix(const QRectF &miniRect,
                                       const QVector3D &charPosition,
                                       bool ignoreRotation) const;
};

/**
 * @brief Reads player position, camera, and game state from GW2's Mumble Link
 * API
 *
 * Exposes all data needed for overlay rendering:
 * - Player position (fAvatarPosition)
 * - Camera position/direction/up (fCameraPosition/Front/Top) — for 3D
 * projection
 * - FOV (from identity JSON) — for perspective matrix
 * - Compass data — for minimap marker rendering
 * - UI state flags — for map-open detection, combat state, etc.
 * - Map ID, character name, world ID
 */
class MumbleLink : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
  Q_PROPERTY(float playerX READ playerX NOTIFY positionChanged)
  Q_PROPERTY(float playerY READ playerY NOTIFY positionChanged)
  Q_PROPERTY(float playerZ READ playerZ NOTIFY positionChanged)
  Q_PROPERTY(uint32_t mapId READ mapId NOTIFY mapChanged)

public:
  explicit MumbleLink(QObject *parent = nullptr);
  explicit MumbleLink(const QString &linkName, QObject *parent = nullptr);
  ~MumbleLink();

  bool start(int updateIntervalMs = 50);
  void stop();
  void setUpdateInterval(int ms);

  /**
   * @brief Switch to a different named MumbleLink segment
   * Stops current link, updates name, reconnects if was running.
   * Use for multibox overlay: setLinkName("GW2MumbleLink2").
   */
  void setLinkName(const QString &name);
  QString linkName() const { return m_linkName; }

  bool isConnected() const { return m_connected; }
  bool isRunning() const { return m_readTimer->isActive(); }

  // Player position (fAvatarPosition — character feet position)
  float playerX() const { return m_playerPos.x(); }
  float playerY() const { return m_playerPos.y(); }
  float playerZ() const { return m_playerPos.z(); }
  QVector3D playerPosition() const { return m_playerPos; }

  // Player facing direction (fAvatarFront — unit vector)
  QVector3D playerFront() const { return m_playerFront; }

  // Camera position (fCameraPosition — where the camera eye is)
  float camPosX() const { return m_camPos.x(); }
  float camPosY() const { return m_camPos.y(); }
  float camPosZ() const { return m_camPos.z(); }
  QVector3D cameraPosition() const { return m_camPos; }

  // Camera direction (fCameraFront — unit vector camera looks toward)
  float camFrontX() const { return m_camFront.x(); }
  float camFrontY() const { return m_camFront.y(); }
  float camFrontZ() const { return m_camFront.z(); }
  QVector3D cameraFront() const { return m_camFront; }

  // Camera up vector (fCameraTop)
  QVector3D cameraTop() const { return m_camTop; }

  // Field of View (from identity JSON, in radians)
  float fov() const { return m_fov; }

  // Map info
  uint32_t mapId() const { return m_mapId; }
  uint32_t mapType() const { return m_mapType; }
  uint32_t mapInstance() const { return m_mapInstance; }
  uint32_t worldId() const { return m_worldId; }

  // Character info
  QString characterName() const { return m_characterName; }
  uint32_t charIdHash() const { return m_charIdHash; }
  QString identity() const { return m_identity; }

  // UI state flags (from MumbleContext.uiState)
  uint32_t uiState() const { return m_uiState; }
  bool isMapOpen() const { return m_uiState & 0x01; }
  bool isMinimapTopRight() const { return (m_uiState & 0x02) != 0; }
  bool isMinimapRotating() const { return (m_uiState & 0x04) != 0; }
  bool gameHasFocus() const { return (m_uiState & 0x08) != 0; }
  bool isPvp() const { return (m_uiState & 0x10) != 0; }
  bool textboxHasFocus() const { return (m_uiState & 0x20) != 0; }
  bool isInCombat() const { return (m_uiState & 0x40) != 0; }

  // Map type detection (from MumbleContext.mapType)
  bool isCharacterSelect() const { return m_mapType == 1; }

  // UI size (from identity JSON: 0=small, 1=normal, 2=large, 3=larger)
  int uiSize() const { return m_uiSize; }

  // Compass data (for minimap rendering)
  const CompassData &minimapData() const { return m_miniMap; }
  const CompassData &bigMapData() const { return m_bigMap; }

  // Mount index
  uint8_t mountIndex() const { return m_mountIndex; }

  // Character identity fields (from identity JSON)
  uint32_t profession() const { return m_profession; }
  uint32_t race() const { return m_race; }
  uint32_t specialization() const { return m_specialization; }

  // Process ID of the GW2 instance
  uint32_t processId() const { return m_processId; }

  // Build ID (game version)
  uint32_t buildId() const { return m_buildId; }

  // UI tick (increments each frame — stall = loading screen)
  uint32_t uiTick() const { return m_lastTick; }

signals:
  void connectionChanged(bool connected);
  void connectedChanged(bool connected); // Alias
  void positionChanged(float x, float y, float z);
  void cameraChanged();
  void mapChanged(uint32_t mapId);
  void dataUpdated(); // Emitted every tick for continuous state monitoring

private slots:
  void readMumbleLink();

private:
  bool openMumbleLink();
  void closeMumbleLink();
  void parseIdentityJson(const QString &identityStr);

#ifdef Q_OS_WIN
  HANDLE m_hMapFile = nullptr;
  LinkedMem *m_linkedMem = nullptr;
#endif

  QString m_linkName = QStringLiteral("MumbleLink");
  QTimer *m_readTimer = nullptr;
  bool m_wasRunning = false; // Track state for setLinkName() reconnect
  bool m_connected = false;
  uint32_t m_lastTick = 0;
  qint64 m_lastTickChangeMs = 0; // Timestamp when uiTick last changed
  static constexpr qint64 STALE_TIMEOUT_MS =
      1000; // 1 second before process check

  // Player data
  QVector3D m_playerPos;
  QVector3D m_playerFront;

  // Camera data
  QVector3D m_camPos;
  QVector3D m_camFront;
  QVector3D m_camTop;
  float m_fov = 0.0f;

  // Map data
  uint32_t m_mapId = 0;
  uint32_t m_mapType = 0;
  uint32_t m_mapInstance = 0;
  uint32_t m_worldId = 0;
  uint32_t m_buildId = 0;

  // Character data
  QString m_characterName;
  uint32_t m_charIdHash = 0;
  QString m_identity;
  uint32_t m_profession = 0;     // 1-9 (profession index)
  uint32_t m_race = 0;           // 0-4 (race index)
  uint32_t m_specialization = 0; // Elite spec ID

  // UI state
  uint32_t m_uiState = 0;
  int m_uiSize = 1; // 0=small, 1=normal, 2=large, 3=larger

  // Compass
  CompassData m_miniMap;
  CompassData m_bigMap;

  // Compass change-detection (per-instance, NOT static — avoids cross-instance
  // log ping-pong when multiple MumbleLink objects have different compass sizes)
  int m_lastLoggedCompassW = 0;
  int m_lastLoggedCompassH = 0;

  // Process
  uint8_t m_mountIndex = 0;
  uint32_t m_processId = 0;
};

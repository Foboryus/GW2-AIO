/**
 * @file MumbleLink.cpp
 * @brief Reads game state from GW2's Mumble Link shared memory
 *
 * Exposes player position, camera position/direction, FOV, compass data,
 * UI state, and character info for overlay rendering.
 *
 * Reference: https://wiki.guildwars2.com/wiki/API:MumbleLink
 * TacO source: MumbleLink.cpp — reading/interpolation approach
 */

#include "MumbleLink.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <cmath>
#include <cstring>

MumbleLink::MumbleLink(QObject *parent)
    : QObject(parent), m_readTimer(new QTimer(this)) {
  m_readTimer->setTimerType(Qt::PreciseTimer);
  connect(m_readTimer, &QTimer::timeout, this, &MumbleLink::readMumbleLink);
}

MumbleLink::MumbleLink(const QString &linkName, QObject *parent)
    : QObject(parent), m_linkName(linkName), m_readTimer(new QTimer(this)) {
  m_readTimer->setTimerType(Qt::PreciseTimer);
  connect(m_readTimer, &QTimer::timeout, this, &MumbleLink::readMumbleLink);
}

MumbleLink::~MumbleLink() { stop(); }

bool MumbleLink::start(int updateIntervalMs) {
  if (!openMumbleLink()) {
    qInfo() << "Mumble Link not yet available:" << m_linkName
            << "- timer started, will retry";
  }

  m_wasRunning = true;
  m_readTimer->start(updateIntervalMs);
  qInfo() << "Mumble Link started:" << m_linkName
          << "interval:" << updateIntervalMs << "ms";
  return true;
}

void MumbleLink::stop() {
  m_wasRunning = false;
  m_readTimer->stop();
  closeMumbleLink();
}

void MumbleLink::setUpdateInterval(int ms) {
  if (m_readTimer->isActive()) {
    m_readTimer->start(ms);
  }
}

void MumbleLink::setLinkName(const QString &name) {
  if (name == m_linkName) {
    return;
  }

  bool wasRunning = m_readTimer->isActive();
  int interval = m_readTimer->interval();

  if (wasRunning) {
    stop();
  }

  m_linkName = name;
  qInfo() << "MumbleLink: Switched to segment:" << m_linkName;

  if (wasRunning) {
    start(interval);
  }
}

bool MumbleLink::openMumbleLink() {
#ifdef Q_OS_WIN
  // Mumble Link protocol: the READER creates the shared memory region.
  // GW2 then opens the same named mapping and writes position data to it.
  // This matches TacO (CreateFileMapping) and Blish HUD (CreateOrOpen).
  // Using OpenFileMappingW would fail with ERROR_FILE_NOT_FOUND because
  // GW2 expects the mapping to already exist.
  m_hMapFile =
      CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                         sizeof(LinkedMem), m_linkName.toStdWString().c_str());

  if (m_hMapFile == nullptr) {
    qWarning() << "MumbleLink: CreateFileMappingW failed for" << m_linkName
               << "error:" << GetLastError();
    return false;
  }

  m_linkedMem = static_cast<LinkedMem *>(
      MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinkedMem)));

  if (m_linkedMem == nullptr) {
    qWarning() << "MumbleLink: MapViewOfFile failed, error:" << GetLastError();
    CloseHandle(m_hMapFile);
    m_hMapFile = nullptr;
    return false;
  }

  qInfo() << "MumbleLink: shared memory ready, segment:" << m_linkName;
  return true;
#else
  return false;
#endif
}

void MumbleLink::closeMumbleLink() {
#ifdef Q_OS_WIN
  if (m_linkedMem != nullptr) {
    UnmapViewOfFile(m_linkedMem);
    m_linkedMem = nullptr;
  }

  if (m_hMapFile != nullptr) {
    CloseHandle(m_hMapFile);
    m_hMapFile = nullptr;
  }
#endif

  if (m_connected) {
    m_connected = false;
    emit connectionChanged(false);
  }
}

void MumbleLink::readMumbleLink() {
#ifdef Q_OS_WIN
  if (m_linkedMem == nullptr) {
    if (!openMumbleLink()) {
      return;
    }
  }

  // Snapshot the entire shared memory struct to prevent torn reads.
  // GW2 writes to this memory from its render thread — without a snapshot,
  // individual field reads can span two different frames (e.g., fAvatarPosition
  // from frame N, context from frame N+1), causing position mismatches →
  // shaking. TacO uses the same approach: memcpy(&lastData, lm,
  // sizeof(LinkedMem))
  LinkedMem snapshot;
  memcpy(&snapshot, m_linkedMem, sizeof(LinkedMem));

  // Check if data has updated
  if (snapshot.uiTick == m_lastTick) {
    // uiTick hasn't changed — check if GW2 process is still alive
    // Only check after 5 seconds of no tick change to minimize syscalls
    if (m_connected && m_processId != 0) {
      qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
      if (m_lastTickChangeMs > 0 &&
          (nowMs - m_lastTickChangeMs) > STALE_TIMEOUT_MS) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   static_cast<DWORD>(m_processId));
        if (hProc == NULL) {
          // GW2 process is gone — mark disconnected
          qInfo() << "MumbleLink: GW2 process" << m_processId
                  << "no longer exists — marking disconnected";
          m_connected = false;
          emit connectionChanged(false);
        } else {
          CloseHandle(hProc);
          // Process still alive — reset timer so we don't spam OpenProcess
          m_lastTickChangeMs = nowMs;
        }
      }
    }
    emit dataUpdated(); // Always emit so overlay can track stall time
    return;
  }

  m_lastTick = snapshot.uiTick;
  m_lastTickChangeMs = QDateTime::currentMSecsSinceEpoch();

  // Check if it's GW2
  QString gameName = QString::fromWCharArray(snapshot.name);
  if (!gameName.contains("Guild Wars 2", Qt::CaseInsensitive)) {
    if (!gameName.isEmpty()) {
      qInfo() << "MumbleLink: game name mismatch:" << gameName;
    }
    if (m_connected) {
      m_connected = false;
      emit connectionChanged(false);
    }
    return;
  }

  // --- Player position (fAvatarPosition) ---
  // Read position BEFORE connection decision (needed for two-part validation)
  QVector3D newPlayerPos(snapshot.fAvatarPosition[0],
                         snapshot.fAvatarPosition[1],
                         snapshot.fAvatarPosition[2]);

  bool posChanged = (newPlayerPos != m_playerPos);
  m_playerPos = newPlayerPos;

  // Player facing direction
  m_playerFront = QVector3D(snapshot.fAvatarFront[0], snapshot.fAvatarFront[1],
                            snapshot.fAvatarFront[2]);

  // --- Camera data (fCameraPosition/Front/Top) ---
  QVector3D newCamPos(snapshot.fCameraPosition[0], snapshot.fCameraPosition[1],
                      snapshot.fCameraPosition[2]);

  QVector3D newCamFront(snapshot.fCameraFront[0], snapshot.fCameraFront[1],
                        snapshot.fCameraFront[2]);

  QVector3D newCamTop(snapshot.fCameraTop[0], snapshot.fCameraTop[1],
                      snapshot.fCameraTop[2]);

  bool camChanged = (newCamPos != m_camPos || newCamFront != m_camFront ||
                     newCamTop != m_camTop);

  m_camPos = newCamPos;
  m_camFront = newCamFront;
  m_camTop = newCamTop;

  // --- Identity JSON (contains FOV, map_id, world_id, uisz, name) ---
  QString identityStr = QString::fromWCharArray(snapshot.identity);
  if (identityStr != m_identity) {
    m_identity = identityStr;
    parseIdentityJson(identityStr);
  }

  // --- Context (contains map ID, UI state, compass data, etc.) ---
  // Note: GW2 sets context_len = 48 (Mumble uses this for server matching),
  // but the full MumbleContext struct (85+ bytes) IS written to shared memory.
  // We read the entire struct from the 256-byte context buffer directly.
  if (snapshot.context_len > 0) {
    const MumbleContext *ctx =
        reinterpret_cast<const MumbleContext *>(snapshot.context);

    m_mapType = ctx->mapType;
    m_mapInstance = ctx->instance;
    m_buildId = ctx->buildId;
    m_processId = ctx->processId;
    m_mountIndex = ctx->mountIndex;

    // --- Two-part validation for overlay activation ---
    // AIO overlay renders ONLY when both conditions are met:
    //   1) Valid map: mapId > 0 AND mapType != 1 (not char select)
    //   2) Valid position: player coordinates are non-zero
    bool validMap = (ctx->mapId > 0 && ctx->mapType != 1);
    bool validPos = !newPlayerPos.isNull();
    bool shouldBeConnected = validMap && validPos;

    if (!shouldBeConnected) {
      // Suppress mapChanged signals during invalid states (char select, loading)
      // to prevent the mapId 65↔19 thrashing that causes thousands of log lines
      m_mapId = ctx->mapId; // Track silently without emitting

      if (m_connected) {
        if (ctx->mapType == 1) {
          qInfo() << "[DEVLOG] MumbleLink: character select detected (mapType=1)"
                     " — pausing overlay";
        } else if (ctx->mapId == 0) {
          qInfo() << "MumbleLink: mapId=0 (loading) — pausing overlay";
        } else if (newPlayerPos.isNull()) {
          qInfo() << "[DEVLOG] MumbleLink: position (0,0,0) — pausing overlay";
        }
        m_connected = false;
        emit connectionChanged(false);
      }
      emit dataUpdated();
      return;
    }

    // Valid state — emit mapChanged if map actually changed
    if (ctx->mapId != m_mapId) {
      m_mapId = ctx->mapId;
      emit mapChanged(m_mapId);
    }

    // Mark connected if transitioning from disconnected
    if (!m_connected) {
      m_connected = true;
      qInfo() << "[DEVLOG] MumbleLink: valid map + position — overlay active"
              << "mapId:" << m_mapId << "mapType:" << m_mapType;
      emit connectionChanged(true);
    }

    // UI state flags
    m_uiState = ctx->uiState;

    // Compass data — minimap vs bigmap depending on map-open state
    // TacO stores minimap data when map is closed, bigmap data when open
    float uiScale = 1.0f;
    if (m_uiSize == 0)
      uiScale = 0.9f;
    else if (m_uiSize == 2)
      uiScale = 1.111f;
    else if (m_uiSize == 3)
      uiScale = 1.224f;

    CompassData &target = isMapOpen() ? m_bigMap : m_miniMap;
    // TacO pattern: compass dims include uiScale multiplication.
    // The real small-window fix is GetWindowTooSmallScale in computeMinimapRect.
    target.compassWidth = static_cast<int>(ctx->compassWidth * uiScale);
    target.compassHeight = static_cast<int>(ctx->compassHeight * uiScale);
    target.compassRotation = ctx->compassRotation;
    target.playerX = ctx->playerX;
    target.playerY = ctx->playerY;
    target.mapCenterX = ctx->mapCenterX;
    target.mapCenterY = ctx->mapCenterY;
    target.mapScale = ctx->mapScale;
    target.uiScale = uiScale;

    // [DEVLOG] compass data change-detection (per-instance member variables)
    if (target.compassWidth != m_lastLoggedCompassW ||
        target.compassHeight != m_lastLoggedCompassH) {
      m_lastLoggedCompassW = target.compassWidth;
      m_lastLoggedCompassH = target.compassHeight;
      qInfo() << "[DEVLOG] MumbleLink: compass changed —"
              << "link:" << m_linkName
              << "raw:" << ctx->compassWidth << "x" << ctx->compassHeight
              << "scaled:" << target.compassWidth << "x" << target.compassHeight
              << "uiScale:" << uiScale << "mapScale:" << target.mapScale
              << "mapOpen:" << isMapOpen() << "uiSize:" << m_uiSize;
    }
  }

  // Emit signals
  if (posChanged) {
    emit positionChanged(m_playerPos.x(), m_playerPos.y(), m_playerPos.z());
  }
  if (camChanged) {
    emit cameraChanged();
  }

  // Always emit dataUpdated so overlay can continuously monitor game state
  emit dataUpdated();
#endif
}

void MumbleLink::parseIdentityJson(const QString &identityStr) {
  // GW2 identity is JSON:
  // {"name":"CharName","profession":1,"spec":18,"race":4,
  //  "map_id":50,"world_id":1001,"team_color_id":0,
  //  "commander":false,"map":50,"fov":0.960,"uisz":1}

  QJsonDocument doc = QJsonDocument::fromJson(identityStr.toUtf8());
  if (!doc.isObject()) {
    return;
  }

  QJsonObject obj = doc.object();

  // FOV (in radians — TacO uses this directly for perspective matrix)
  if (obj.contains("fov")) {
    m_fov = static_cast<float>(obj["fov"].toDouble(0.0));
  }

  // Character name + hash (for per-character activation tracking)
  if (obj.contains("name")) {
    QString newName = obj["name"].toString();
    if (newName != m_characterName) {
      m_characterName = newName;
      // Simple hash matching TacO's approach
      m_charIdHash = qHash(m_characterName);
    }
  }

  // World ID
  if (obj.contains("world_id")) {
    m_worldId = static_cast<uint32_t>(obj["world_id"].toInt(0));
  }

  // UI size
  if (obj.contains("uisz")) {
    m_uiSize = obj["uisz"].toInt(1);
  }

  // Profession (1-9: Guardian, Warrior, Engineer, Ranger, Thief,
  //             Elementalist, Mesmer, Necromancer, Revenant)
  if (obj.contains("profession")) {
    m_profession = static_cast<uint32_t>(obj["profession"].toInt(0));
  }

  // Race (0=Asura, 1=Charr, 2=Human, 3=Norn, 4=Sylvari)
  if (obj.contains("race")) {
    m_race = static_cast<uint32_t>(obj["race"].toInt(0));
  }

  // Elite specialization ID
  if (obj.contains("spec")) {
    m_specialization = static_cast<uint32_t>(obj["spec"].toInt(0));
  }
}

// --- CompassData ---

QMatrix4x4 CompassData::buildTransformationMatrix(const QRectF &miniRect,
                                                  const QVector3D &charPosition,
                                                  bool ignoreRotation,
                                                  float windowTooSmallScale) const {
  // Exact replication of TacO's CompassData::BuildTransformationMatrix
  // Converts world (Mumble) coordinates → pixel coordinates on the minimap
  //
  // IMPORTANT: TacO uses row-vector convention (pos * M), Qt uses column-vector
  // (M * pos). In Qt, the LAST transform added via operator* is applied FIRST
  // to the point. We build the composite matrix so that (result = M * pos)
  // matches TacO's (result = pos * M_taco).
  //
  // TacO's chain (applied left-to-right on pos):
  //   1. World→game scale  2. Center on player  3. Flip Y
  //   4. Compass rotation   5. 1/24 scale        6. MapCenter offset
  //   7. mapScale + uiScale 8. Translate to rect center
  //
  // Qt must chain these in REVERSE order (last *= is applied first):

  const float worldToGame = 1.0f / 0.0254f;
  float rotation = ignoreRotation ? 0.0f : compassRotation;

  // --- Step 8: Translate to minimap center on screen ---
  QMatrix4x4 result;
  result.setToIdentity();
  result.translate(static_cast<float>(miniRect.center().x()),
                   static_cast<float>(miniRect.center().y()), 0.0f);

  // --- Step 7: Scale by 1/mapScale ---
  // Note: uiScale is already applied to compassWidth/Height.
  // Small-window compensation is handled by windowTooSmallScale in
  // computeMinimapRect(), NOT here.
  if (mapScale > 0.0f) {
    float msf = 1.0f / mapScale;
    result.scale(msf, msf, 1.0f);
  }

  // --- Step 6: MapCenter offset ---
  // TacO: offset = -((mapCenter - playerPos) * windowTooSmallScale).Rotated(rotation)
  // where playerPos comes from CompassData (continent coords), NOT charPosition
  // NOTE: windowTooSmallScale is NOT applied here yet — needs investigation.
  // Applying it directly breaks minimap rendering (player icon / border disappear).
  // The rect already compensates for WTS in computeMinimapRect(), so applying
  // it again here may double-scale. Needs careful TacO cross-reference.
  Q_UNUSED(windowTooSmallScale);
  float offsetX = -(mapCenterX - playerX);
  float offsetY = -(mapCenterY - playerY);

  // Rotate the offset by compass rotation
  if (rotation != 0.0f) {
    float cosR = std::cos(rotation);
    float sinR = std::sin(rotation);
    float rx = offsetX * cosR - offsetY * sinR;
    float ry = offsetX * sinR + offsetY * cosR;
    offsetX = rx;
    offsetY = ry;
  }

  result.translate(offsetX, offsetY, 0.0f);

  // --- Step 5: Scale by 1/24 (TacO's magic constant) ---
  float s24 = 1.0f / 24.0f;
  result.scale(s24, s24, 1.0f);

  // --- Step 4: Compass rotation ---
  if (rotation != 0.0f) {
    result.rotate(qRadiansToDegrees(rotation), 0.0f, 0.0f, 1.0f);
  }

  // --- Step 3: Flip Y ---
  result.scale(1.0f, -1.0f, 1.0f);

  // --- Step 2: Center on player (translate to player position in game coords)
  // ---
  float mapOffsetX = charPosition.x() * worldToGame;
  float mapOffsetZ = charPosition.z() * worldToGame;
  result.translate(-mapOffsetX, -mapOffsetZ, 0.0f);

  // --- Step 1: World→game scale ---
  // TacO maps (X, Y, Z, 1) → (X*scale, Z*scale, 0, 1)
  // Y (height) discarded; Z (depth) becomes 2D Y axis
  // Qt column-vector: M*pos, so row 1 col 2 = worldToGame maps Z→screenY
  QMatrix4x4 worldScale(worldToGame, 0, 0, 0, 0, 0, worldToGame, 0, 0, 0, 0, 0,
                        0, 0, 0, 1);

  result = result * worldScale;

  return result;
}

float CompassData::computeWindowTooSmallScale(int screenW, int screenH) {
  constexpr float kMinW = 1024.0f;
  constexpr float kMinH = 768.0f;
  float wtsW = (screenW < static_cast<int>(kMinW))
                   ? static_cast<float>(screenW) / kMinW
                   : 1.0f;
  float wtsH = (screenH < static_cast<int>(kMinH))
                   ? static_cast<float>(screenH) / kMinH
                   : 1.0f;
  return qMin(wtsW, wtsH);
}

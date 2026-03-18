/**
 * @file TrailPipeline.cpp
 * @brief 3D trail billboard strip rendering implementation
 *
 * Builds camera-facing billboard strip meshes from trail waypoints:
 * 1. For each pair of adjacent waypoints, compute a camera-facing quad
 * 2. UV is mapped along the trail length (U) and across width (V)
 * 3. Vertex alpha fades at trail start/end
 * 4. Entire mesh is submitted with view/projection from MumbleLink
 *
 * Trail mesh generation algorithm (inspired by Blish HUD Pathing module, MIT):
 *   For each segment [P_i, P_i+1]:
 *     direction = normalize(P_i+1 - P_i)
 *     right = normalize(cross(direction, up))  or cross(direction, toCam)
 *     left  = P_i - right * halfWidth
 *     right = P_i + right * halfWidth
 *   → Generate triangle strip: left[i], right[i], left[i+1], right[i+1]
 */

#include "TrailPipeline.h"
#include "D3D11Context.h"

#include "core/MumbleLink.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerModels.h"
#include "features/markers/MarkerSettingsManager.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>

// ============================================================================
// Constructor / Destructor
// ============================================================================

TrailPipeline::TrailPipeline(D3D11Context *context, MumbleLink *mumble,
                             MarkerManager *manager,
                             MarkerSettingsManager *settings,
                             ImageCache *imageCache)
    : m_context(context), m_mumble(mumble), m_markerManager(manager),
      m_markerSettings(settings), m_imageCache(imageCache) {}

TrailPipeline::~TrailPipeline() = default;

// ============================================================================
// Initialization
// ============================================================================

bool TrailPipeline::initialize() {
  if (!m_context || !m_context->isInitialized()) {
    return false;
  }

  // Load and compile trail shader
  QFile shaderFile(":/shaders/trail.hlsl");
  QByteArray shaderSource;

  if (shaderFile.open(QIODevice::ReadOnly)) {
    shaderSource = shaderFile.readAll();
    shaderFile.close();
  } else {
    QFile localFile("shaders/trail.hlsl");
    if (localFile.open(QIODevice::ReadOnly)) {
      shaderSource = localFile.readAll();
      localFile.close();
    } else {
      qCritical() << "TrailPipeline: Could not load trail.hlsl";
      return false;
    }
  }

  // Compile shaders
  QString vsError;
  auto vsBlob =
      m_context->compileShader(shaderSource, "VSMain", "vs_5_0", vsError);
  if (!vsBlob) {
    qCritical() << "TrailPipeline VS failed:" << vsError;
    return false;
  }

  HRESULT hr = m_context->device()->CreateVertexShader(
      vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
      m_vertexShader.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  QString psError;
  auto psBlob =
      m_context->compileShader(shaderSource, "PSMain", "ps_5_0", psError);
  if (!psBlob) {
    qCritical() << "TrailPipeline PS failed:" << psError;
    return false;
  }

  hr = m_context->device()->CreatePixelShader(psBlob->GetBufferPointer(),
                                              psBlob->GetBufferSize(), nullptr,
                                              m_pixelShader.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // Input layout
  D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA,
       0},
  };

  hr = m_context->device()->CreateInputLayout(
      layoutDesc, _countof(layoutDesc), vsBlob->GetBufferPointer(),
      vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // Constant buffer
  D3D11_BUFFER_DESC cbDesc = {};
  cbDesc.ByteWidth = sizeof(PerFrameCB);
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = m_context->device()->CreateBuffer(&cbDesc, nullptr,
                                         m_perFrameCB.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // Default texture
  m_defaultTexture = m_context->createDefaultWhiteTexture();

  // Trail sampler — WRAP for both U and V (matching TacO)
  // U wraps across width, V wraps along trail length for scroll animation.
  {
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    HRESULT samplerHr = m_context->device()->CreateSamplerState(
        &samplerDesc, m_sampler.GetAddressOf());
    if (FAILED(samplerHr)) {
      qWarning() << "TrailPipeline: Failed to create WRAP sampler";
      return false;
    }
  }

  // Rasterizer: no back-face culling (TacO renders trails double-sided)
  {
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE; // Both sides visible
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = TRUE;
    HRESULT hr = m_context->device()->CreateRasterizerState(
        &rastDesc, m_rasterizerNoCull.GetAddressOf());
    if (FAILED(hr)) {
      qWarning() << "TrailPipeline: Failed to create CULL_NONE rasterizer";
      return false;
    }
  }

  // Rasterizer: no back-face culling + scissor enabled (for minimap clipping)
  {
    D3D11_RASTERIZER_DESC scissorDesc = {};
    scissorDesc.FillMode = D3D11_FILL_SOLID;
    scissorDesc.CullMode = D3D11_CULL_NONE;
    scissorDesc.FrontCounterClockwise = FALSE;
    scissorDesc.DepthClipEnable = FALSE; // No depth clip for 2D minimap
    scissorDesc.ScissorEnable = TRUE;
    HRESULT hr = m_context->device()->CreateRasterizerState(
        &scissorDesc, m_rasterizerScissor.GetAddressOf());
    if (FAILED(hr)) {
      qWarning() << "TrailPipeline: Failed to create scissor rasterizer";
      return false;
    }
  }

  // Start animation timer
  m_timer.start();

  m_initialized = true;
  qInfo() << "TrailPipeline initialized";
  return true;
}

// ============================================================================
// Trail Mesh Generation
// ============================================================================

void TrailPipeline::buildTrailMesh(const Trail &trail, TrailMesh &mesh) {
  const auto &points = trail.points;
  if (points.size() < 2) {
    mesh.vertexCount = 0;
    mesh.indexCount = 0;
    return;
  }

  float halfWidth = trail.trailScale > 0.0f ? trail.trailScale * 0.5f : 0.5f;
  int segmentCount = points.size() - 1;

  // Two vertices per waypoint (left and right of the strip)
  QVector<TrailVertex> vertices;
  vertices.reserve(points.size() * 2);

  QVector<uint16_t> indices;
  indices.reserve(segmentCount * 6);

  float cumulativeU = 0.0f;
  int vertexIndex = 0; // Current vertex pair index (only valid points)
  bool sectionStart =
      true; // True when the next valid point starts a new section

  for (int i = 0; i < points.size(); i++) {
    const QVector3D &pos = points[i];

    // TacO convention: (0,0,0) sentinel marks a trail break (teleport gate,
    // loading screen, etc.). Skip the sentinel and start a new section.
    // (TacO source: GW2Trail::Build, TrailLogger.cpp lines 774-781)
    if (pos.isNull()) {
      sectionStart = true;
      continue;
    }

    // Peek at next valid point (skip sentinels) for direction calculation
    QVector3D nextValid;
    bool hasNext = false;
    for (int j = i + 1; j < points.size(); j++) {
      if (!points[j].isNull()) {
        nextValid = points[j];
        hasNext = true;
        break;
      }
    }

    // Compute direction along trail
    QVector3D dir;
    if (hasNext && !sectionStart) {
      dir = (nextValid - pos).normalized();
    } else if (hasNext) {
      dir = (nextValid - pos).normalized();
    } else if (vertexIndex > 0) {
      // Last point in the trail — use direction from previous point
      dir = (pos - points[i - 1]).normalized();
    } else {
      dir = QVector3D(1, 0, 0); // Fallback
    }

    // World-up flat ribbon: cross(direction, UP) gives a horizontal right
    // vector. This creates ground-following ribbons visible from any camera
    // angle (no need for per-frame mesh rebuilds like camera-facing billboards)
    QVector3D right =
        QVector3D::crossProduct(dir, QVector3D(0, 1, 0)).normalized();

    // If direction is nearly vertical, fall back to cross with forward
    if (right.lengthSquared() < 0.001f) {
      right = QVector3D::crossProduct(dir, QVector3D(0, 0, 1)).normalized();
    }

    // Left and right vertices
    QVector3D leftPos = pos - right * halfWidth;
    QVector3D rightPos = pos + right * halfWidth;

    // Cumulative V for texture mapping along trail length
    // TacO convention: U = across trail width (0=left, 1=right)
    //                  V = along trail length (scrolling)
    if (!sectionStart && vertexIndex > 0) {
      // Find the previous valid point for UV distance
      for (int j = i - 1; j >= 0; j--) {
        if (!points[j].isNull()) {
          cumulativeU += (pos - points[j]).length();
          break;
        }
      }
    }

    // Per-vertex alpha: fade at start and end of trail
    float alpha = 1.0f;
    if (i < 3) {
      alpha = static_cast<float>(i) / 3.0f;
    } else if (i >= points.size() - 3) {
      alpha = static_cast<float>(points.size() - 1 - i) / 3.0f;
    }

    // TacO convention: V is NEGATIVE along trail direction (-uvStretch)
    // This ensures the scroll animation goes forward and texture isn't flipped
    vertices.append(
        {leftPos.x(), leftPos.y(), leftPos.z(), 0.0f, -cumulativeU, alpha});
    vertices.append(
        {rightPos.x(), rightPos.y(), rightPos.z(), 1.0f, -cumulativeU, alpha});

    // Generate quad indices ONLY within a continuous section (not across
    // breaks)
    if (!sectionStart && vertexIndex > 0) {
      int base = (vertexIndex - 1) * 2;
      // Quad: base+0, base+1, base+2, base+2, base+1, base+3
      indices.append(static_cast<uint16_t>(base + 0));
      indices.append(static_cast<uint16_t>(base + 1));
      indices.append(static_cast<uint16_t>(base + 2));
      indices.append(static_cast<uint16_t>(base + 2));
      indices.append(static_cast<uint16_t>(base + 1));
      indices.append(static_cast<uint16_t>(base + 3));
    }

    vertexIndex++;
    sectionStart = false;
  }

  // Create GPU buffers
  D3D11_BUFFER_DESC vbDesc = {};
  vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(TrailVertex));
  vbDesc.Usage = D3D11_USAGE_DEFAULT;
  vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA vbData = {};
  vbData.pSysMem = vertices.constData();

  mesh.vertexBuffer.Reset();
  HRESULT hr = m_context->device()->CreateBuffer(
      &vbDesc, &vbData, mesh.vertexBuffer.GetAddressOf());
  if (FAILED(hr)) {
    return;
  }

  D3D11_BUFFER_DESC ibDesc = {};
  ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
  ibDesc.Usage = D3D11_USAGE_DEFAULT;
  ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

  D3D11_SUBRESOURCE_DATA ibData = {};
  ibData.pSysMem = indices.constData();

  mesh.indexBuffer.Reset();
  hr = m_context->device()->CreateBuffer(&ibDesc, &ibData,
                                         mesh.indexBuffer.GetAddressOf());
  if (FAILED(hr)) {
    return;
  }

  mesh.vertexCount = vertices.size();
  mesh.indexCount = indices.size();
  mesh.dirty = false;
}

// ============================================================================
// Texture Management
// ============================================================================

ID3D11ShaderResourceView *
TrailPipeline::getOrCreateTexture(const QString &texturePath) {
  if (texturePath.isEmpty()) {
    return m_defaultTexture.Get();
  }

  // Check cache (std::unordered_map — QHash breaks ComPtr refcounting)
  std::string key = texturePath.toStdString();
  auto it = m_textureCache.find(key);
  if (it != m_textureCache.end()) {
    it->second.lastUsedFrame = m_frameCount;
    return it->second.srv.Get();
  }

  if (!m_imageCache) {
    return m_defaultTexture.Get();
  }

  QImage image = m_imageCache->getImage(texturePath);
  if (image.isNull()) {
    m_textureCache[key] =
        {m_defaultTexture, m_frameCount}; // Cache the default to avoid retries
    return m_defaultTexture.Get();
  }

  QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
  auto srv = m_context->createTextureFromRGBA(rgba.width(), rgba.height(),
                                              rgba.constBits());
  if (!srv) {
    return m_defaultTexture.Get();
  }

  m_textureCache[key] = {srv, m_frameCount};

  // Free CPU-side QImage — SRV is cached in m_textureCache, no re-read needed
  m_imageCache->evict(texturePath);

  return srv.Get();
}

void TrailPipeline::sweepUnusedTextures() {
  constexpr uint64_t kMaxAge = 300; // ~5s at 60fps
  int evicted = 0;

  for (auto it = m_textureCache.begin(); it != m_textureCache.end();) {
    if (m_frameCount - it->second.lastUsedFrame > kMaxAge) {
      it = m_textureCache.erase(it);
      evicted++;
    } else {
      ++it;
    }
  }

  if (evicted > 0) {
    qInfo() << "TrailPipeline: Evicted" << evicted
            << "unused textures (LRU sweep)";
  }
}

void TrailPipeline::preloadTextures() {
  if (!m_initialized || !m_showTrails || !m_markerManager) {
    return;
  }

  auto trails = m_markerManager->getVisibleTrails();
  if (trails.isEmpty()) {
    return;
  }

  // Pre-load all unique trail textures BEFORE the D3D11 frame begins
  QSet<QString> seen;
  for (const Trail *trail : trails) {
    if (!trail->texturePath.isEmpty() && !seen.contains(trail->texturePath)) {
      seen.insert(trail->texturePath);
      getOrCreateTexture(trail->texturePath);
    }
  }
}

// ============================================================================
// Render
// ============================================================================

void TrailPipeline::render() {
  ++m_frameCount;

  // Periodic LRU sweep (every 300 frames)
  if (m_frameCount % 300 == 0) {
    sweepUnusedTextures();
  }

  if (!m_initialized || !m_showTrails || !m_mumble || !m_markerManager) {
    return;
  }

  // TacO: !mumbleLink.IsValid() — hide overlay when GW2 is not running
  if (!m_mumble->isConnected()) {
    return;
  }

  if (m_mumble->isMapOpen()) {
    return;
  }

  // Get visible trails for current map (pre-filtered by MarkerManager)
  auto trails = m_markerManager->getVisibleTrails();



  if (trails.isEmpty()) {
    return;
  }

  auto *ctx = m_context->context();

  // Build view/projection (same as MarkerPipeline)
  // Use interpolated camera override if set (Optimization 3), else raw MumbleLink
  auto camPos = m_useCameraOverride ? m_camOverridePos
                                    : m_mumble->cameraPosition();
  auto camFront = m_useCameraOverride ? m_camOverrideFront
                                      : m_mumble->cameraFront();

  DirectX::XMVECTOR eye =
      DirectX::XMVectorSet(camPos.x(), camPos.y(), camPos.z(), 0.0f);
  DirectX::XMVECTOR target =
      DirectX::XMVectorSet(camPos.x() + camFront.x(), camPos.y() + camFront.y(),
                           camPos.z() + camFront.z(), 0.0f);
  DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

  DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, target, up);

  float fov = m_mumble->fov();
  if (fov <= 0.0f)
    fov = 1.222f;
  float aspect = static_cast<float>(m_context->width()) /
                 static_cast<float>(m_context->height());
  if (aspect <= 0.0f)
    aspect = 16.0f / 9.0f;

  DirectX::XMMATRIX proj =
      DirectX::XMMatrixPerspectiveFovLH(fov, aspect, 0.01f, 1000.0f);
  DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);

  // Animation time (seconds)
  float time = m_timer.elapsed() / 1000.0f;

  // Player position for near-player fade
  auto pPos = m_usePlayerOverride ? m_playerOverridePos
                                  : m_mumble->playerPosition();
  DirectX::XMFLOAT4 playerPos = {pPos.x(), pPos.y(), pPos.z(), 0.0f};

  // Pre-compute viewProjection and view matrices
  DirectX::XMFLOAT4X4 vpStored, viewStored;
  DirectX::XMStoreFloat4x4(&vpStored, viewProj);
  DirectX::XMStoreFloat4x4(&viewStored, view);

  // Set pipeline state (once, before per-trail loop)
  ctx->IASetInputLayout(m_inputLayout.Get());
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  // TacO: CULL_NONE — trail ribbons must be visible from both sides
  ctx->RSSetState(m_rasterizerNoCull.Get());

  ctx->VSSetShader(m_vertexShader.Get(), nullptr, 0);
  ctx->PSSetShader(m_pixelShader.Get(), nullptr, 0);

  ID3D11Buffer *cbs[] = {m_perFrameCB.Get()};
  ctx->VSSetConstantBuffers(0, 1, cbs);
  ctx->PSSetConstantBuffers(0, 1, cbs);

  ID3D11SamplerState *samplers[] = {m_sampler.Get()};
  ctx->PSSetSamplers(0, 1, samplers);

  // Render each trail with per-trail constant buffer update
  int drawCount = 0;

  for (const Trail *trail : trails) {

    // Get or build trail mesh (keyed by type + data path)
    std::string meshKey =
        (trail->type + "|" + trail->trailDataPath).toStdString();

    auto &mesh = m_meshCache[meshKey]; // auto-inserts default if missing
    if (mesh.dirty) {
      buildTrailMesh(*trail, mesh);

    }

    if (mesh.indexCount <= 0) {
      continue;
    }

    // Update constant buffer with per-trail fade values
    // TacO pattern: re-map CB before each trail draw call
    {
      D3D11_MAPPED_SUBRESOURCE mapped;
      HRESULT hr =
          ctx->Map(m_perFrameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      if (SUCCEEDED(hr)) {
        auto *cb = static_cast<PerFrameCB *>(mapped.pData);
        cb->viewProjection = vpStored;
        cb->viewMatrix = viewStored;
        cb->time = time;
        cb->trailOpacity = m_opacity;
        cb->nearFadeStart = 3.0f; // Near-player fade start (meters)
        cb->nearFadeEnd = 4.0f;   // Near-player fade end (meters)
        cb->playerPosition = playerPos;

        // Per-trail far-fade from pack data
        // TacO conversion: game-inches × 0.0254 = meters
        // Default -1 means no fade (shader handles negative = no fade)
        cb->farFadeNear =
            trail->fadeNear >= 0.0f ? trail->fadeNear * 0.0254f : -1.0f;
        cb->farFadeFar =
            trail->fadeFar >= 0.0f ? trail->fadeFar * 0.0254f : -1.0f;

        // Global max render distance from user settings
        cb->maxRenderDist =
            m_markerSettings
                ? static_cast<float>(m_markerSettings->maxRenderDistance())
                : m_maxRenderDistance;
        cb->minimap2D = 0.0f; // 3D world mode

        ctx->Unmap(m_perFrameCB.Get(), 0);
      }
    }

    // Bind trail texture
    auto *srv = getOrCreateTexture(trail->texturePath);
    ID3D11ShaderResourceView *srvs[] = {srv};
    ctx->PSSetShaderResources(0, 1, srvs);

    // Bind trail mesh
    UINT stride = sizeof(TrailVertex);
    UINT offset = 0;
    ID3D11Buffer *vbs[] = {mesh.vertexBuffer.Get()};
    ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    ctx->IASetIndexBuffer(mesh.indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

    // Draw
    ctx->DrawIndexed(static_cast<UINT>(mesh.indexCount), 0, 0);
    drawCount++;
  }

}

// ============================================================================
// Minimap Rect Computation
// ============================================================================

QRectF TrailPipeline::computeMinimapRect() const {
  if (!m_mumble || !m_mumble->isConnected()) {
    return QRectF();
  }

  const CompassData &compass = m_mumble->minimapData();
  if (compass.compassWidth <= 0 || compass.compassHeight <= 0) {
    return QRectF();
  }

  float screenW = static_cast<float>(m_context->width());
  float screenH = static_cast<float>(m_context->height());
  float cw = static_cast<float>(compass.compassWidth);
  float ch = static_cast<float>(compass.compassHeight);

  float x, y;
  if (m_mumble->isMinimapTopRight()) {
    x = screenW - cw;
    y = 1.0f;
  } else {
    // Default: bottom-right with UI-size-dependent delta
    int uiSize = m_mumble->uiSize();
    int delta = 37; // Normal
    if (uiSize == 0)
      delta = 33; // Small
    else if (uiSize == 2)
      delta = 41; // Large
    else if (uiSize == 3)
      delta = 45; // Larger

    x = screenW - cw;
    y = screenH - ch - static_cast<float>(delta);
  }

  return QRectF(static_cast<double>(x), static_cast<double>(y),
                static_cast<double>(cw), static_cast<double>(ch));
}

// ============================================================================
// Minimap Trail Mesh Generation
// ============================================================================

void TrailPipeline::buildMinimapTrailMesh(const Trail &trail, TrailMesh &mesh,
                                          float worldWidth) {
  const auto &points = trail.points;
  if (points.size() < 2) {
    mesh.vertexCount = 0;
    mesh.indexCount = 0;
    return;
  }

  // Half-width in world-space (meters)
  float halfWidth = worldWidth * 0.5f;

  QVector<TrailVertex> vertices;
  vertices.reserve(points.size() * 2);

  QVector<uint16_t> indices;
  indices.reserve(points.size() * 6);

  float cumulativeU = 0.0f;
  int vertexIndex = 0;
  bool sectionStart = true;

  for (int i = 0; i < points.size(); i++) {
    const QVector3D &pos = points[i];

    // TacO convention: (0,0,0) sentinel marks section break (teleport gate)
    if (pos.isNull()) {
      sectionStart = true;
      continue;
    }

    // Find next valid point for direction calculation
    int nextIdx = -1;
    for (int j = i + 1; j < points.size(); j++) {
      if (!points[j].isNull()) {
        nextIdx = j;
        break;
      }
    }

    // Find previous valid point for fallback direction
    int prevIdx = -1;
    for (int j = i - 1; j >= 0; j--) {
      if (!points[j].isNull()) {
        prevIdx = j;
        break;
      }
    }

    // Compute direction in XZ plane (world-space, Y is vertical)
    float dx, dz;
    if (nextIdx >= 0) {
      dx = points[nextIdx].x() - pos.x();
      dz = points[nextIdx].z() - pos.z();
    } else if (prevIdx >= 0) {
      dx = pos.x() - points[prevIdx].x();
      dz = pos.z() - points[prevIdx].z();
    } else {
      dx = 1.0f;
      dz = 0.0f;
    }

    // Normalize XZ direction
    float len = std::sqrt(dx * dx + dz * dz);
    if (len > 0.0001f) {
      dx /= len;
      dz /= len;
    }

    // Perpendicular in XZ plane: rotate 90 degrees
    // direction (dx, dz) → perpendicular (-dz, dx)
    float rx = -dz * halfWidth;
    float rz = dx * halfWidth;

    // Build flat ribbon in world space — Y stays at the trail's Y coordinate
    // Left vertex: pos - perpendicular
    float leftX = pos.x() - rx;
    float leftZ = pos.z() - rz;
    // Right vertex: pos + perpendicular
    float rightX = pos.x() + rx;
    float rightZ = pos.z() + rz;

    // Accumulate UV distance along trail (world-space distance)
    if (!sectionStart && prevIdx >= 0) {
      cumulativeU += (pos - points[prevIdx]).length();
    }

    // Per-vertex alpha: fade at trail endpoints
    float alpha = 1.0f;
    if (i < 3) {
      alpha = static_cast<float>(i) / 3.0f;
    } else if (i >= points.size() - 3) {
      alpha = static_cast<float>(points.size() - 1 - i) / 3.0f;
    }

    // Vertex positions are world-space (x, y, z)
    // The VS will transform via ViewProjection (worldToNDC, transposed)
    // Scale V by worldWidth to normalize texture density for minimap:
    // In 3D, raw world-meter UV works because the trail is wide on screen.
    // On the minimap, trails are tiny — dividing by worldWidth makes each
    // texture repeat span ~1 trail-width, preserving arrow visibility.
    float scaledV = -cumulativeU / worldWidth;
    vertices.append({leftX, pos.y(), leftZ, 0.0f, scaledV, alpha});
    vertices.append({rightX, pos.y(), rightZ, 1.0f, scaledV, alpha});

    // Only emit indices within continuous sections (not after sentinel)
    if (!sectionStart && vertexIndex > 0) {
      int base = (vertexIndex - 1) * 2;
      indices.append(static_cast<uint16_t>(base + 0));
      indices.append(static_cast<uint16_t>(base + 1));
      indices.append(static_cast<uint16_t>(base + 2));
      indices.append(static_cast<uint16_t>(base + 2));
      indices.append(static_cast<uint16_t>(base + 1));
      indices.append(static_cast<uint16_t>(base + 3));
    }

    vertexIndex++;
    sectionStart = false;
  }

  if (vertices.isEmpty()) {
    mesh.vertexCount = 0;
    mesh.indexCount = 0;
    return;
  }

  // Create GPU buffers
  D3D11_BUFFER_DESC vbDesc = {};
  vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(TrailVertex));
  vbDesc.Usage = D3D11_USAGE_DEFAULT;
  vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA vbData = {};
  vbData.pSysMem = vertices.constData();

  mesh.vertexBuffer.Reset();
  HRESULT hr = m_context->device()->CreateBuffer(
      &vbDesc, &vbData, mesh.vertexBuffer.GetAddressOf());
  if (FAILED(hr)) {
    return;
  }

  D3D11_BUFFER_DESC ibDesc = {};
  ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
  ibDesc.Usage = D3D11_USAGE_DEFAULT;
  ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

  D3D11_SUBRESOURCE_DATA ibData = {};
  ibData.pSysMem = indices.constData();

  mesh.indexBuffer.Reset();
  hr = m_context->device()->CreateBuffer(&ibDesc, &ibData,
                                         mesh.indexBuffer.GetAddressOf());
  if (FAILED(hr)) {
    return;
  }

  mesh.vertexCount = vertices.size();
  mesh.indexCount = indices.size();
  mesh.dirty = false;
}

// ============================================================================
// Minimap/BigMap Render Pass
// ============================================================================

void TrailPipeline::renderMinimap() {
  if (!m_initialized || !m_mumble || !m_markerManager) {
    return;
  }

  if (!m_mumble->isConnected()) {
    return;
  }

  // Determine minimap vs big map mode
  bool bigMap = m_mumble->isMapOpen();

  // Rendering layer toggles — skip if this layer is disabled
  if (bigMap && !m_showBigMap)
    return;
  if (!bigMap && !m_showMinimap)
    return;

  // Get compass data
  const CompassData &compass =
      bigMap ? m_mumble->bigMapData() : m_mumble->minimapData();
  if (compass.compassWidth <= 0 || compass.compassHeight <= 0) {
    return;
  }

  // Compute render rect
  float screenW = static_cast<float>(m_context->width());
  float screenH = static_cast<float>(m_context->height());

  QRectF renderRect;
  if (bigMap) {
    // Big map: full screen
    renderRect = QRectF(0, 0, static_cast<double>(screenW),
                        static_cast<double>(screenH));
  } else {
    renderRect = computeMinimapRect();
  }

  if (renderRect.isEmpty()) {
    return;
  }

  // Get visible trails — use map-specific context so trails with
  // inGameVisibility=0 but miniMapVisibility/mapVisibility=1 still render
  RenderContext trailCtx =
      bigMap ? RenderContext::BigMap : RenderContext::Minimap;
  auto trails = m_markerManager->getVisibleTrails(trailCtx);
  if (trails.isEmpty()) {
    return;
  }

  // --- Build world→NDC transformation matrix ---
  // Step 1: buildTransformationMatrix gives world→pixel coords (Qt col-vec)
  QMatrix4x4 worldToPixel = compass.buildTransformationMatrix(
      renderRect, m_mumble->playerPosition(), bigMap);

  // Step 2: Pixel→NDC using viewport-based approach
  // The D3D11 viewport is set to renderRect, so NDC [-1,1] maps directly
  // to the render area. This preserves aspect ratio for non-square rects.
  float miniW = static_cast<float>(renderRect.width());
  float miniH = static_cast<float>(renderRect.height());
  float miniX = static_cast<float>(renderRect.x());
  float miniY = static_cast<float>(renderRect.y());

  QMatrix4x4 pixelToNDC;
  pixelToNDC.translate(-1.0f, 1.0f, 0.5f);
  pixelToNDC.scale(2.0f / miniW, -2.0f / miniH, 1.0f);
  pixelToNDC.translate(-miniX, -miniY, 0.0f);

  QMatrix4x4 worldToNDC = pixelToNDC * worldToPixel;



  // Step 3: Convert Qt matrix → DirectX XMFLOAT4X4
  // QMatrix4x4::constData() returns column-major floats (OpenGL convention).
  // HLSL row_major float4x4 with mul(v, M) expects row-major storage.
  // Column-major of M_colVec == row-major of M_rowVec (transpose relationship).
  // So constData() can be memcpy'd directly — no transpose needed.
  DirectX::XMFLOAT4X4 viewProjDX;
  memcpy(&viewProjDX, worldToNDC.constData(), sizeof(DirectX::XMFLOAT4X4));

  // Compass scale: worldToPixel = (1/0.0254) * (1/24) * (1/mapScale)
  // Inverse: pixelToWorld = 0.0254 * 24 * mapScale = 0.6096 * mapScale
  // For N minimap pixels: worldWidth = N * 0.6096 * mapScale
  float mapScale = compass.mapScale > 0.0f ? compass.mapScale : 1.0f;
  float pixelToWorld = 0.6096f * mapScale;
  float defaultPixelWidth =
      (bigMap ? 6.0f : 4.0f) * m_minimapTrailWidth; // user multiplier
  float defaultWorldWidth = defaultPixelWidth * pixelToWorld;

  // Invalidate minimap mesh cache when rendering parameters change.
  // Meshes are world-space: the worldToNDC matrix handles zoom/pan per-frame.
  // Only rebuild when trail geometry actually changes (map ID, width, mode).
  // mapScale is NOT a trigger: zoom is handled by the matrix, and removing
  // it prevents synchronous mesh rebuilds on every scroll-wheel tick.
  uint32_t currentMap = m_mumble->mapId();
  bool cacheInvalid = false;

  if (currentMap != m_lastMinimapMapId) {
    m_lastMinimapMapId = currentMap;
    cacheInvalid = true;
  }
  if (m_minimapTrailWidth != m_lastMinimapWidthCached) {
    m_lastMinimapWidthCached = m_minimapTrailWidth;
    cacheInvalid = true;
  }
  if (bigMap != m_lastBigMap) {
    m_lastBigMap = bigMap;
    cacheInvalid = true;
  }

  if (cacheInvalid) {
    m_minimapMeshCache.clear();
  }

  // --- Set D3D11 state ---
  auto *ctx = m_context->context();

  // Save current rasterizer state and viewport
  ComPtr<ID3D11RasterizerState> savedRasterizer;
  ctx->RSGetState(savedRasterizer.GetAddressOf());
  D3D11_VIEWPORT savedViewport;
  UINT numViewports = 1;
  ctx->RSGetViewports(&numViewports, &savedViewport);

  // Set viewport to render rect (replaces scissor — preserves aspect ratio)
  D3D11_VIEWPORT vp = {};
  vp.TopLeftX = static_cast<float>(renderRect.left());
  vp.TopLeftY = static_cast<float>(renderRect.top());
  vp.Width = miniW;
  vp.Height = miniH;
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  ctx->RSSetViewports(1, &vp);

  // Use no-cull rasterizer (no scissor needed with viewport)
  ctx->RSSetState(m_rasterizerNoCull.Get());

  // Set pipeline state
  ctx->IASetInputLayout(m_inputLayout.Get());
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(m_vertexShader.Get(), nullptr, 0);
  ctx->PSSetShader(m_pixelShader.Get(), nullptr, 0);

  ID3D11Buffer *cbs[] = {m_perFrameCB.Get()};
  ctx->VSSetConstantBuffers(0, 1, cbs);
  ctx->PSSetConstantBuffers(0, 1, cbs);

  ID3D11SamplerState *samplers[] = {m_sampler.Get()};
  ctx->PSSetSamplers(0, 1, samplers);

  // Animation time
  float time = m_timer.elapsed() / 1000.0f;

  int drawCount = 0;

  for (const Trail *trail : trails) {
    // Get or build minimap mesh (cached in world-space)
    std::string meshKey =
        (trail->type + "|" + trail->trailDataPath + "|minimap").toStdString();

    auto &mesh = m_minimapMeshCache[meshKey];

    // Only rebuild if cache was invalidated or mesh is new (empty)
    if (mesh.indexCount <= 0) {
      float worldW = defaultWorldWidth;
      if (trail->trailScale > 0.0f) {
        float trailPixelWidth = 4.0f * trail->trailScale * m_minimapTrailWidth;
        worldW = trailPixelWidth * pixelToWorld;
      }

      buildMinimapTrailMesh(*trail, mesh, worldW);

    }

    if (mesh.indexCount <= 0) {
      continue;
    }

    // Update constant buffer — minimap2D mode with worldToNDC matrix
    {
      D3D11_MAPPED_SUBRESOURCE mapped;
      HRESULT hr =
          ctx->Map(m_perFrameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      if (SUCCEEDED(hr)) {
        auto *cb = static_cast<PerFrameCB *>(mapped.pData);
        cb->viewProjection = viewProjDX; // World→NDC (transposed for DX)
        cb->viewMatrix = viewProjDX;     // Same for WorldPos passthrough
        cb->time = time * trail->animSpeed;
        cb->trailOpacity = m_minimapOpacity * trail->alpha;
        cb->nearFadeStart = 0.0f;
        cb->nearFadeEnd = 0.0f;
        cb->playerPosition = {0.0f, 0.0f, 0.0f, 0.0f};
        cb->farFadeNear = -1.0f;
        cb->farFadeFar = -1.0f;
        cb->maxRenderDist = 0.0f;
        cb->minimap2D = 1.0f; // Skip 3D fades

        ctx->Unmap(m_perFrameCB.Get(), 0);
      }
    }

    // Bind trail texture
    auto *srv = getOrCreateTexture(trail->texturePath);
    ID3D11ShaderResourceView *srvs[] = {srv};
    ctx->PSSetShaderResources(0, 1, srvs);

    // Bind mesh
    UINT stride = sizeof(TrailVertex);
    UINT offset = 0;
    ID3D11Buffer *vbs[] = {mesh.vertexBuffer.Get()};
    ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    ctx->IASetIndexBuffer(mesh.indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

    // Draw
    ctx->DrawIndexed(static_cast<UINT>(mesh.indexCount), 0, 0);
    drawCount++;
  }

  // Restore previous rasterizer state and viewport
  ctx->RSSetState(savedRasterizer.Get());
  ctx->RSSetViewports(1, &savedViewport);


}

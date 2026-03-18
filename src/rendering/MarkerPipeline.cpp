/**
 * @file MarkerPipeline.cpp
 * @brief 3D marker billboard rendering implementation
 *
 * Implements TacO's marker projection algorithm:
 * 1. Build view matrix from MumbleLink camera (LookAt)
 * 2. Build projection matrix from MumbleLink FOV (Perspective)
 * 3. For each marker on the current map:
 *    a. Compute camera-space distance → alpha fade
 *    b. Submit billboard quad with texture and fade color
 * 4. Marker shader handles projection + billboard expansion
 *
 * TacO reference (from source analysis, CC-BY-NC — reimplemented):
 *   cam.SetLookAtLH(camPos, camPos + camDir, up(0,1,0))
 *   persp.SetPerspectiveFovLH(fov, aspect, 0.01, 1000.0)
 *   camspace = worldPos * cam → filter by distance → screenpos = camspace *
 * persp
 */

#include "MarkerPipeline.h"
#include "D3D11Context.h"
#include "ExclusionData.h"
#include "GlyphAtlas.h"
#include "SpriteBatch.h"

#include "core/MumbleLink.h"
#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerModels.h"
#include "features/markers/MarkerSettingsManager.h"
#include <QFileInfo>
#include <QSet>

#include <QDebug>
#include <QFile>
#include <QImage>

#include <algorithm>

// ============================================================================
// Constructor / Destructor
// ============================================================================

MarkerPipeline::MarkerPipeline(D3D11Context *context, MumbleLink *mumble,
                               MarkerManager *manager,
                               MarkerSettingsManager *settings,
                               ImageCache *imageCache)
    : m_context(context), m_mumble(mumble), m_markerManager(manager),
      m_markerSettings(settings), m_imageCache(imageCache) {}

MarkerPipeline::~MarkerPipeline() = default;

// ============================================================================
// Initialization
// ============================================================================

bool MarkerPipeline::initialize() {
  if (!m_context || !m_context->isInitialized()) {
    qWarning() << "MarkerPipeline: D3D11Context not initialized";
    return false;
  }

  // --- Load and compile marker shader ---

  QFile shaderFile(":/shaders/marker.hlsl");
  QByteArray shaderSource;

  if (shaderFile.open(QIODevice::ReadOnly)) {
    shaderSource = shaderFile.readAll();
    shaderFile.close();
  } else {
    QFile localFile("shaders/marker.hlsl");
    if (localFile.open(QIODevice::ReadOnly)) {
      shaderSource = localFile.readAll();
      localFile.close();
    } else {
      qCritical() << "MarkerPipeline: Could not load marker.hlsl";
      return false;
    }
  }

  // Compile vertex shader
  QString vsError;
  auto vsBlob =
      m_context->compileShader(shaderSource, "VSMain", "vs_5_0", vsError);
  if (!vsBlob) {
    qCritical() << "MarkerPipeline VS compilation failed:" << vsError;
    return false;
  }

  HRESULT hr = m_context->device()->CreateVertexShader(
      vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
      m_vertexShader.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // Compile pixel shader
  QString psError;
  auto psBlob =
      m_context->compileShader(shaderSource, "PSMain", "ps_5_0", psError);
  if (!psBlob) {
    qCritical() << "MarkerPipeline PS compilation failed:" << psError;
    return false;
  }

  hr = m_context->device()->CreatePixelShader(psBlob->GetBufferPointer(),
                                              psBlob->GetBufferSize(), nullptr,
                                              m_pixelShader.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // --- Input layout ---

  D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  hr = m_context->device()->CreateInputLayout(
      layoutDesc, _countof(layoutDesc), vsBlob->GetBufferPointer(),
      vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // --- Quad vertex buffer (unit billboard: -0.5 to 0.5) ---

  struct QuadVertex {
    float x, y; // Position
    float u, v; // TexCoord
  };

  QuadVertex quadVerts[] = {
      {-0.5f, -0.5f, 0.0f, 1.0f}, // Bottom-left
      {0.5f, -0.5f, 1.0f, 1.0f},  // Bottom-right
      {0.5f, 0.5f, 1.0f, 0.0f},   // Top-right
      {-0.5f, 0.5f, 0.0f, 0.0f},  // Top-left
  };

  D3D11_BUFFER_DESC vbDesc = {};
  vbDesc.ByteWidth = sizeof(quadVerts);
  vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
  vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA vbData = {};
  vbData.pSysMem = quadVerts;

  hr = m_context->device()->CreateBuffer(&vbDesc, &vbData,
                                         m_quadVertexBuffer.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // --- Quad index buffer ---

  uint16_t indices[] = {0, 1, 2, 2, 3, 0};

  D3D11_BUFFER_DESC ibDesc = {};
  ibDesc.ByteWidth = sizeof(indices);
  ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
  ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

  D3D11_SUBRESOURCE_DATA ibData = {};
  ibData.pSysMem = indices;

  hr = m_context->device()->CreateBuffer(&ibDesc, &ibData,
                                         m_quadIndexBuffer.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // --- Constant buffers ---

  D3D11_BUFFER_DESC cbDesc = {};
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  cbDesc.ByteWidth = sizeof(PerFrameCB);
  hr = m_context->device()->CreateBuffer(&cbDesc, nullptr,
                                         m_perFrameCB.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  cbDesc.ByteWidth = sizeof(PerMarkerCB);
  hr = m_context->device()->CreateBuffer(&cbDesc, nullptr,
                                         m_perMarkerCB.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // --- Default texture + sampler ---

  m_defaultTexture = m_context->createDefaultWhiteTexture();
  m_sampler = m_context->createLinearSampler();

  m_initialized = true;
  qInfo() << "MarkerPipeline initialized";
  return true;
}

// ============================================================================
// View/Projection Matrices (TacO algorithm)
// ============================================================================

DirectX::XMMATRIX MarkerPipeline::buildViewMatrix() const {
  if (!m_mumble) {
    return DirectX::XMMatrixIdentity();
  }

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

  return DirectX::XMMatrixLookAtLH(eye, target, up);
}

DirectX::XMMATRIX MarkerPipeline::buildProjectionMatrix() const {
  if (!m_mumble || !m_context) {
    return DirectX::XMMatrixIdentity();
  }

  float fov = m_mumble->fov();
  if (fov <= 0.0f) {
    fov = 1.222f; // Default ~70 degrees
  }

  float aspect = static_cast<float>(m_context->width()) /
                 static_cast<float>(m_context->height());
  if (aspect <= 0.0f) {
    aspect = 16.0f / 9.0f;
  }

  // TacO uses near=0.01, far=1000
  return DirectX::XMMatrixPerspectiveFovLH(fov, aspect, 0.01f, 1000.0f);
}

float MarkerPipeline::computeDistanceFade(float distance, float fadeNear,
                                          float fadeFar) const {
  if (fadeFar <= 0.0f) {
    return 1.0f; // No distance fade
  }

  if (distance >= fadeFar) {
    return 0.0f; // Fully faded
  }

  if (fadeNear <= 0.0f || distance <= fadeNear) {
    return 1.0f; // Fully visible
  }

  // Linear interpolation between fadeNear and fadeFar
  return 1.0f - (distance - fadeNear) / (fadeFar - fadeNear);
}

// ============================================================================
// Texture Management
// ============================================================================

ID3D11ShaderResourceView *
MarkerPipeline::getOrCreateTexture(const QString &iconPath) {
  if (iconPath.isEmpty()) {
    return m_defaultTexture.Get();
  }

  // Check cache (std::unordered_map — QHash breaks ComPtr refcounting)
  std::string key = iconPath.toStdString();
  auto it = m_textureCache.find(key);
  if (it != m_textureCache.end()) {
    it->second.lastUsedFrame = m_frameCount;
    return it->second.srv.Get();
  }

  // Try to load from ImageCache
  if (!m_imageCache) {
    return m_defaultTexture.Get();
  }

  QImage image = m_imageCache->getImage(iconPath);
  if (image.isNull()) {
    return m_defaultTexture.Get();
  }

  // Convert to RGBA8888 for D3D11
  QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);

  auto srv = m_context->createTextureFromRGBA(rgba.width(), rgba.height(),
                                              rgba.constBits());
  if (!srv) {
    return m_defaultTexture.Get();
  }

  m_textureCache[key] = {srv, m_frameCount};

  // Free CPU-side QImage — SRV is cached in m_textureCache, no re-read needed
  m_imageCache->evict(iconPath);

  return srv.Get();
}

// ============================================================================
// LRU Texture Eviction
// ============================================================================

void MarkerPipeline::sweepUnusedTextures() {
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
    qInfo() << "MarkerPipeline: Evicted" << evicted
            << "unused textures (LRU sweep)";
  }
}

// ============================================================================
// Render
// ============================================================================

// ============================================================================
// Texture Pre-loading
// ============================================================================

void MarkerPipeline::preloadTextures() {
  if (!m_initialized || !m_showMarkers || !m_markerManager) {
    return;
  }

  auto markers = m_markerManager->getVisibleMarkers();
  if (markers.isEmpty()) {
    return;
  }

  // Pre-load all unique textures BEFORE the D3D11 frame begins.
  // Textures created during an active frame (between beginFrame/endFrame)
  // silently fail to render.
  QSet<QString> seen;
  for (const Marker *marker : markers) {
    if (!marker->iconPath.isEmpty() && !seen.contains(marker->iconPath)) {
      seen.insert(marker->iconPath);
      getOrCreateTexture(marker->iconPath);
    }
  }
}

void MarkerPipeline::render() {
  ++m_frameCount;

  // Periodic LRU sweep (every 300 frames)
  if (m_frameCount % 300 == 0) {
    sweepUnusedTextures();
  }

  // Debug: log once when we first get a real mapId (not 0)
  static uint32_t lastDebugMapId = 0;
  uint32_t curMapId = m_mumble ? m_mumble->mapId() : 0;
  if (curMapId != lastDebugMapId) {
    lastDebugMapId = curMapId;
    int visCount =
        (m_markerManager ? m_markerManager->getVisibleMarkers().size() : -1);
    qInfo() << "MarkerPipeline: mapId changed to" << curMapId
            << "init:" << m_initialized << "show:" << m_showMarkers
            << "mapOpen:" << (m_mumble ? m_mumble->isMapOpen() : false)
            << "packs:"
            << (m_markerManager ? m_markerManager->packs().size() : 0)
            << "visible:" << visCount;
  }

  if (!m_initialized || !m_showMarkers || !m_mumble || !m_markerManager) {
    return;
  }

  // TacO: !mumbleLink.IsValid() — hide overlay when GW2 is not running
  if (!m_mumble->isConnected()) {
    return;
  }

  // Don't render if map is open or game loading
  if (m_mumble->isMapOpen()) {
    return;
  }

  // Get visible markers for current map (pre-filtered by MarkerManager)
  auto markers = m_markerManager->getVisibleMarkers();
  if (markers.isEmpty()) {
    return;
  }

  auto *ctx = m_context->context();

  // --- Build view/projection matrices ---

  DirectX::XMMATRIX view = buildViewMatrix();
  DirectX::XMMATRIX proj = buildProjectionMatrix();
  DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);

  // --- Update per-frame constant buffer ---

  {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr =
        ctx->Map(m_perFrameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
      auto *cb = static_cast<PerFrameCB *>(mapped.pData);
      DirectX::XMStoreFloat4x4(&cb->viewProjection, viewProj);
      cb->screenWidth = static_cast<float>(m_context->width());
      cb->screenHeight = static_cast<float>(m_context->height());
      float fov = m_mumble->fov();
      if (fov <= 0.0f)
        fov = 1.222f; // Default ~70 degrees
      float fovScale = 1.0f / tanf(fov * 0.5f);
      cb->fovScale = fovScale;
      ctx->Unmap(m_perFrameCB.Get(), 0);

      // Cache for CPU-side projected size (distance labels)
      m_cachedFovScale = fovScale;
    }
  }

  // --- Set pipeline state ---

  ctx->IASetInputLayout(m_inputLayout.Get());

  UINT stride = sizeof(float) * 4; // 2 floats pos + 2 floats texcoord
  UINT offset = 0;
  ID3D11Buffer *vbs[] = {m_quadVertexBuffer.Get()};
  ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
  ctx->IASetIndexBuffer(m_quadIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  ctx->VSSetShader(m_vertexShader.Get(), nullptr, 0);
  ctx->PSSetShader(m_pixelShader.Get(), nullptr, 0);

  ID3D11Buffer *cbs[] = {m_perFrameCB.Get()};
  ctx->VSSetConstantBuffers(0, 1, cbs);

  ID3D11SamplerState *samplers[] = {m_sampler.Get()};
  ctx->PSSetSamplers(0, 1, samplers);

  // --- Camera position for distance sorting ---
  // Use interpolated override if set (Optimization 3)
  auto camPos3D = m_useCameraOverride ? m_camOverridePos
                                      : m_mumble->cameraPosition();
  DirectX::XMVECTOR camPosVec =
      DirectX::XMVectorSet(camPos3D.x(), camPos3D.y(), camPos3D.z(), 0.0f);

  // --- Sort markers back-to-front for proper alpha blending ---

  struct MarkerSort {
    const Marker *marker;
    float distance;
  };

  QVector<MarkerSort> sortedMarkers;
  sortedMarkers.reserve(markers.size());

  for (const Marker *marker : markers) {
    // MarkerManager already filters by map and visibility,
    // but we still need distance culling
    QVector3D pos = marker->position();
    DirectX::XMVECTOR markerPos =
        DirectX::XMVectorSet(pos.x(), pos.y(), pos.z(), 0.0f);
    DirectX::XMVECTOR diff = DirectX::XMVectorSubtract(markerPos, camPosVec);
    float dist = DirectX::XMVectorGetX(DirectX::XMVector3Length(diff));

    // Distance culling — use global max render distance (user slider)
    // If pack specifies fadeFar >= 0, also cull beyond that
    float cullDist = m_maxRenderDistance;
    if (marker->fadeFar >= 0.0f) {
      // Pack-specific cull distance (in game-inches → meters)
      float packFadeFar = marker->fadeFar * 0.0254f;
      cullDist = qMin(cullDist, packFadeFar);
    }
    if (dist > cullDist) {
      continue;
    }

    sortedMarkers.append({marker, dist});
  }

  // Sort back-to-front (furthest first -> painter's algorithm)
  std::sort(sortedMarkers.begin(), sortedMarkers.end(),
            [](const MarkerSort &a, const MarkerSort &b) {
              return a.distance > b.distance;
            });


  // --- Render each marker ---

  // Collect distance labels for 2D pass after billboard rendering
  struct DistanceLabel {
    float screenX, screenY;
    float alpha;
    QString text;
  };
  QVector<DistanceLabel> distanceLabels;

  // Avatar position for player-to-marker distance (not camera)
  QVector3D avatarPos;
  if (m_showDistance && m_spriteBatch && m_glyphAtlas) {
    avatarPos = m_mumble->playerPosition();
    distanceLabels.reserve(sortedMarkers.size());
  }

  float screenW = static_cast<float>(m_context->width());
  float screenH = static_cast<float>(m_context->height());

  for (const auto &sorted : sortedMarkers) {
    const Marker *marker = sorted.marker;
    float dist = sorted.distance;

    // Distance fade — two independent fade factors
    float fade = 1.0f;

    // 1. Pack-specific fade (TacO-matching): only when both fadeNear/fadeFar >=
    // 0
    if (marker->fadeNear >= 0.0f && marker->fadeFar >= 0.0f) {
      float packFadeNear = marker->fadeNear * 0.0254f; // game-inches → meters
      float packFadeFar = marker->fadeFar * 0.0254f;
      fade *= computeDistanceFade(dist, packFadeNear, packFadeFar);
    }

    // 2. Global max render distance smooth fade (last 10% of range)
    if (m_maxRenderDistance > 0.0f) {
      float fadeStart = m_maxRenderDistance * 0.9f;
      if (dist > fadeStart) {
        float globalFade =
            1.0f -
            (dist - fadeStart) / qMax(m_maxRenderDistance - fadeStart, 0.01f);
        fade *= qMax(0.0f, globalFade);
      }
    }

    if (fade <= 0.001f) {
      continue;
    }

    // Update per-marker constant buffer
    {
      D3D11_MAPPED_SUBRESOURCE mapped;
      HRESULT hr =
          ctx->Map(m_perMarkerCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      if (SUCCEEDED(hr)) {
        auto *cb = static_cast<PerMarkerCB *>(mapped.pData);
        QVector3D pos = marker->position();
        cb->worldPosition = {pos.x(), pos.y() + marker->heightOffset, pos.z()};
        cb->size =
            (marker->iconSize > 0.0f ? marker->iconSize : 1.0f) * m_markerScale;

        // Tint = marker alpha * distance fade * pipeline opacity
        float alpha = marker->alpha * fade * m_opacity;
        cb->tintColor = {1.0f, 1.0f, 1.0f, alpha};

        cb->minSize = qMax(marker->minSize, 16.0f); // Floor at 16px
        cb->maxSize = marker->maxSize > 0.0f ? marker->maxSize : 256.0f;

        ctx->Unmap(m_perMarkerCB.Get(), 0);
      }
    }

    // Bind per-marker CB to slot b1
    ID3D11Buffer *markerCBs[] = {m_perMarkerCB.Get()};
    ctx->VSSetConstantBuffers(1, 1, markerCBs);

    // Bind marker texture (pre-loaded before beginFrame)
    auto *srv = getOrCreateTexture(marker->iconPath);
    ID3D11ShaderResourceView *srvs[] = {srv};
    ctx->PSSetShaderResources(0, 1, srvs);

    // Draw the billboard quad
    ctx->DrawIndexed(6, 0, 0);

    // Collect distance label for 2D pass
    if (m_showDistance && m_spriteBatch && m_glyphAtlas) {
      QVector3D markerPos = marker->position();
      markerPos.setY(markerPos.y() + marker->heightOffset);
      float playerDist =
          markerPos.distanceToPoint(avatarPos); // meters (MumbleLink)

      // CPU-side world → screen projection
      DirectX::XMVECTOR worldPos = DirectX::XMVectorSet(
          markerPos.x(), markerPos.y(), markerPos.z(), 1.0f);
      DirectX::XMVECTOR clipPos =
          DirectX::XMVector4Transform(worldPos, viewProj);
      float w = DirectX::XMVectorGetW(clipPos);
      if (w > 0.01f) {
        float ndcX = DirectX::XMVectorGetX(clipPos) / w;
        float ndcY = DirectX::XMVectorGetY(clipPos) / w;
        float sx = (ndcX * 0.5f + 0.5f) * screenW;
        float sy = (-ndcY * 0.5f + 0.5f) * screenH;

        // Format: "42.7m" for <10m, "42m" for >=10m
        QString text;
        if (playerDist < 10.0f) {
          text = QString::number(static_cast<double>(playerDist), 'f', 1) +
                 QStringLiteral("m");
        } else {
          text = QString::number(static_cast<int>(playerDist)) +
                 QStringLiteral("m");
        }

        // Compute projected marker size on CPU (same formula as marker.hlsl)
        // to find the marker's bottom edge on screen
        float iconSize =
            (marker->iconSize > 0.0f ? marker->iconSize : 1.0f) * m_markerScale;
        float projectedSize =
            (iconSize / w) * screenH * 0.5f * m_cachedFovScale;
        float minSz = qMax(marker->minSize, 16.0f);
        float maxSz = marker->maxSize > 0.0f ? marker->maxSize : 256.0f;
        projectedSize = qBound(minSz, projectedSize, maxSz);

        // Position at marker bottom edge + user-configurable gap
        float labelY = sy + projectedSize * 0.5f + m_distanceLabelOffset;

        // Apply exclusion zone fade (mirrors HLSL computeExclusionAlpha)
        // Pass label half-dimensions so the zone check expands to cover
        // the full text extent, not just the anchor point
        float textW = m_glyphAtlas->measureText(text);
        float labelHalfW = textW * 0.5f;
        constexpr float kLabelHeight = 16.0f; // Approximate glyph height
        float labelHalfH = kLabelHeight * 0.5f;
        float exAlpha = computeExclusionAlpha(sx, labelY, screenW, screenH,
                                              labelHalfW, labelHalfH);
        float alpha = marker->alpha * fade * m_opacity * exAlpha;

        if (alpha <= 0.001f) {
          continue;
        }

        distanceLabels.append({sx, labelY, alpha, text});
      }
    }
  }

  // --- 2D pass: draw distance labels ---

  if (m_showDistance && m_spriteBatch && m_glyphAtlas &&
      !distanceLabels.isEmpty()) {
    m_spriteBatch->begin();
    for (const auto &label : distanceLabels) {
      float textW = m_glyphAtlas->measureText(label.text);
      float x = label.screenX - textW / 2.0f; // Center horizontally
      int a = qBound(0, static_cast<int>(255 * label.alpha), 255);
      m_glyphAtlas->drawString(m_spriteBatch, label.text, x, label.screenY,
                               QColor(255, 255, 255, a));
    }
    m_spriteBatch->end();
  }
}

// ============================================================================
// Exclusion Zone Alpha (CPU-side — mirrors HLSL computeExclusionAlpha)
// ============================================================================

float MarkerPipeline::computeExclusionAlpha(float sx, float sy, float screenW,
                                            float screenH, float labelHalfW,
                                            float labelHalfH) const {
  if (!m_markerSettings || !m_markerSettings->exclusionEnabled()) {
    return 1.0f;
  }

  if (screenW <= 0.0f || screenH <= 0.0f) {
    return 1.0f;
  }

  // Normalize screen position to [0..1] (same as HLSL: uv = svPos / ScreenSize)
  float uvX = sx / screenW;
  float uvY = sy / screenH;

  // Convert label half-dimensions to percentage coords
  float padX = labelHalfW / screenW;
  float padY = labelHalfH / screenH;

  float fadeEdge = m_markerSettings->exclusionFadeEdge();

  // Build zone list (same as D3D11OverlayWindow::updateAndBindExclusionZones)
  DirectX::XMFLOAT4 zones[kMaxExclusionZones];
  int zoneCount = 0;

  // --- Predefined zone: Minimap ---
  if (m_markerSettings->minimapZoneEnabled() && m_mumble &&
      m_mumble->isConnected()) {
    const auto &compass = m_mumble->minimapData();
    if (compass.compassWidth > 0 && compass.compassHeight > 0 &&
        zoneCount < kMaxExclusionZones) {
      float cw = static_cast<float>(compass.compassWidth) / screenW;
      float ch = static_cast<float>(compass.compassHeight) / screenH;

      float cx, cy;
      if (m_mumble->isMinimapTopRight()) {
        cx = 1.0f - cw;
        cy = 0.0f;
      } else {
        constexpr float kBottomBarPx = 36.0f;
        float bottomOffset = kBottomBarPx / screenH;
        cx = 1.0f - cw;
        cy = 1.0f - ch - bottomOffset;
      }

      zones[zoneCount] = {cx, cy, cw, ch};
      zoneCount++;
    }
  }

  // --- Predefined zone: Skill Bar ---
  if (m_markerSettings->skillBarZoneEnabled() &&
      zoneCount < kMaxExclusionZones) {
    if (m_markerSettings->hasPredefinedOverride("SkillBar")) {
      auto ov = m_markerSettings->predefinedOverride("SkillBar");
      zones[zoneCount] = {ov.x, ov.y, ov.w, ov.h};
    } else {
      constexpr float kSkillBarMaxW = 768.0f;
      constexpr float kSkillBarMaxH = 96.0f;
      float sbW = qMin(0.40f, kSkillBarMaxW / screenW);
      float sbH = qMin(0.09f, kSkillBarMaxH / screenH);
      float sbX = 0.5f - sbW / 2.0f;
      float sbY = 1.0f - sbH;
      zones[zoneCount] = {sbX, sbY, sbW, sbH};
    }
    zoneCount++;
  }

  // --- Predefined zone: Chat Box ---
  if (m_markerSettings->chatZoneEnabled() &&
      zoneCount < kMaxExclusionZones) {
    if (m_markerSettings->hasPredefinedOverride("Chat")) {
      auto ov = m_markerSettings->predefinedOverride("Chat");
      zones[zoneCount] = {ov.x, ov.y, ov.w, ov.h};
    } else {
      zones[zoneCount] = {0.0f, 0.75f, 0.28f, 0.25f};
    }
    zoneCount++;
  }

  // --- Custom zones ---
  const auto &customZones = m_markerSettings->customZones();
  for (const auto &zone : customZones) {
    if (zoneCount >= kMaxExclusionZones)
      break;
    zones[zoneCount] = {zone.x, zone.y, zone.w, zone.h};
    zoneCount++;
  }

  if (zoneCount <= 0) {
    return 1.0f;
  }

  // Mirror HLSL computeExclusionAlpha: check each zone
  // Expand each zone by the label's half-dimensions so the fade triggers
  // when the text edge approaches the zone, not just the anchor point
  float minAlpha = 1.0f;

  for (int i = 0; i < zoneCount; ++i) {
    float zoneMinX = zones[i].x - padX;
    float zoneMinY = zones[i].y - padY;
    float zoneMaxX = zones[i].x + zones[i].z + padX;
    float zoneMaxY = zones[i].y + zones[i].w + padY;

    // Inside zone = hard kill (alpha 0)
    if (uvX >= zoneMinX && uvX <= zoneMaxX &&
        uvY >= zoneMinY && uvY <= zoneMaxY) {
      return 0.0f;
    }

    // Fade fringe outside the zone
    if (fadeEdge > 0.0f) {
      float outerMinX = zoneMinX - fadeEdge;
      float outerMinY = zoneMinY - fadeEdge;
      float outerMaxX = zoneMaxX + fadeEdge;
      float outerMaxY = zoneMaxY + fadeEdge;

      if (uvX >= outerMinX && uvX <= outerMaxX &&
          uvY >= outerMinY && uvY <= outerMaxY) {
        // Distance from pixel to nearest zone edge (outside)
        float dx = qMax(zoneMinX - uvX, uvX - zoneMaxX);
        float dy = qMax(zoneMinY - uvY, uvY - zoneMaxY);
        float edgeDist = qMax(qMax(dx, 0.0f), qMax(dy, 0.0f));

        // Fade: 0.0 at zone edge -> 1.0 at fadeEdge distance outside
        float fade = qBound(0.0f, edgeDist / fadeEdge, 1.0f);
        minAlpha = qMin(minAlpha, fade);
      }
    }
  }

  return minAlpha;
}

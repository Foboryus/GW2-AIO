/**
 * @file RadialRenderer.cpp
 * @brief GPU resource manager for radial wheel D3D11 rendering
 *
 * Compiles HLSL shaders from QRC, creates constant buffers, loads textures,
 * and provides draw methods for the radial wheel rendering pipeline.
 */

#include "RadialRenderer.h"
#include "RadialElement.h"
#include "RadialWheel.h"
#include "rendering/D3D11Context.h"

#include <QDebug>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <cstring>

// ============================================================================
// Constant Buffer Structures (must match HLSL layout exactly)
// ============================================================================

static constexpr int kMaxElements = 20;

// VS b0: RadialQuadCB
struct QuadCBData {
  float spriteDimensions[4]; // xy = NDC center, zw = NDC half-size
  float tiltMatrix[16];      // 4x4 column-major
  float spriteZ;
  float _pad[3];
};

// PS b0: RadialWheelCB
struct WheelCBData {
  float wipeMaskData[3];
  float wheelFadeIn;
  float animationTimer;
  float centerScale;
  int elementCount;
  float globalOpacity;
  float hoverFadeIns[kMaxElements]; // Packed as float4[5]
  float timeLeft;
  int showIcon; // HLSL bool → 4 bytes
  float _pad[2];
};

// PS b1: RadialElementCB
struct ElementCBData {
  float adjustedColor[4];
  float elementHoverFadeIn;
  int premultiplyAlpha; // HLSL bool → 4 bytes
  float _pad[2];
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

RadialRenderer::RadialRenderer() = default;

RadialRenderer::~RadialRenderer() { shutdown(); }

// ============================================================================
// Initialization
// ============================================================================

bool RadialRenderer::initialize(D3D11Context *ctx) {
  if (m_initialized) {
    return true;
  }

  if (!compileShaders(ctx)) {
    qCritical() << "RadialRenderer: Shader compilation failed";
    return false;
  }

  if (!createConstantBuffers(ctx)) {
    qCritical() << "RadialRenderer: Constant buffer creation failed";
    return false;
  }

  if (!createBackgroundTexture(ctx)) {
    qCritical() << "RadialRenderer: Background texture creation failed";
    return false;
  }

  m_sampler = ctx->createLinearSampler();
  if (!m_sampler) {
    qCritical() << "RadialRenderer: Sampler creation failed";
    return false;
  }

  // Use the context's existing alpha blend state
  // (premultiplied alpha for DComp compositing)
  m_blendState.Reset(); // Will use ctx->alphaBlendState() directly

  m_initialized = true;
  qInfo() << "RadialRenderer: Initialized successfully";
  return true;
}

void RadialRenderer::shutdown() {
  m_radialVS.Reset();
  m_wheelPS.Reset();
  m_elementPS.Reset();
  m_cursorPS.Reset();
  m_delayPS.Reset();
  m_quadCB.Reset();
  m_wheelCB.Reset();
  m_elemCB.Reset();
  m_backgroundSRV.Reset();
  m_sampler.Reset();
  m_blendState.Reset();
  m_initialized = false;
}

// ============================================================================
// Texture Loading
// ============================================================================

ComPtr<ID3D11ShaderResourceView>
RadialRenderer::loadIconTexture(D3D11Context *ctx, const QString &qrcPath) {
  constexpr int kIconSize = 128;
  QImage rgba(kIconSize, kIconSize, QImage::Format_RGBA8888);
  rgba.fill(Qt::transparent);

  if (qrcPath.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
    // SVG: rasterize via QSvgRenderer to 128x128 RGBA
    QSvgRenderer svgRenderer(qrcPath);
    if (!svgRenderer.isValid()) {
      qWarning() << "RadialRenderer: Invalid SVG:" << qrcPath;
      return nullptr;
    }
    QPainter painter(&rgba);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    svgRenderer.render(&painter);
    painter.end();
  } else {
    // PNG/other raster: load and convert
    QImage source(qrcPath);
    if (source.isNull()) {
      qWarning() << "RadialRenderer: Failed to load icon:" << qrcPath;
      return nullptr;
    }
    rgba = source.scaled(kIconSize, kIconSize, Qt::KeepAspectRatio,
                          Qt::SmoothTransformation)
               .convertToFormat(QImage::Format_RGBA8888);
  }

  auto srv = ctx->createTextureFromRGBA(rgba.width(), rgba.height(),
                                         rgba.constBits());
  if (!srv) {
    qWarning() << "RadialRenderer: Failed to create texture for:" << qrcPath;
    return nullptr;
  }

  qInfo() << "RadialRenderer: Loaded icon" << qrcPath
          << rgba.width() << "x" << rgba.height();
  return srv;
}

// ============================================================================
// Shader Compilation
// ============================================================================

bool RadialRenderer::compileShaders(D3D11Context *ctx) {
  static const QString kQrcBase = QStringLiteral(":/shaders/radial");
  QString errorMsg;

  // Helper: load QRC file → QByteArray
  auto loadShaderSource = [](const QString &path) -> QByteArray {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      qCritical() << "RadialRenderer: Failed to load shader:" << path;
      return QByteArray();
    }
    return file.readAll();
  };

  // --- Vertex shader (no includes — standalone) ---
  {
    QByteArray src = loadShaderSource(kQrcBase + "/radial_vs.hlsl");
    if (src.isEmpty()) {
      return false;
    }
    auto blob = ctx->compileShader(src, "RadialVS", "vs_5_0", errorMsg);
    if (!blob) {
      qCritical() << "RadialRenderer: VS compile failed:" << errorMsg;
      return false;
    }
    HRESULT hr = ctx->device()->CreateVertexShader(
        blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
        m_radialVS.GetAddressOf());
    if (FAILED(hr)) {
      qCritical() << "RadialRenderer: CreateVertexShader failed:" << Qt::hex
                  << hr;
      return false;
    }
  }

  // --- Pixel shaders (all include radial_common.hlsli via QRC) ---
  struct PSEntry {
    const char *file;
    const char *entry;
    ComPtr<ID3D11PixelShader> *target;
  };

  PSEntry psEntries[] = {
      {"radial_wheel_ps.hlsl", "RadialWheelPS", &m_wheelPS},
      {"radial_element_ps.hlsl", "RadialElementPS", &m_elementPS},
      {"radial_cursor_ps.hlsl", "RadialCursorPS", &m_cursorPS},
      {"radial_delay_ps.hlsl", "RadialDelayPS", &m_delayPS},
  };

  for (auto &entry : psEntries) {
    QByteArray src = loadShaderSource(kQrcBase + "/" + entry.file);
    if (src.isEmpty()) {
      return false;
    }
    auto blob = ctx->compileShaderWithIncludes(src, entry.entry, "ps_5_0",
                                               kQrcBase, errorMsg);
    if (!blob) {
      qCritical() << "RadialRenderer:" << entry.file << "compile failed:"
                  << errorMsg;
      return false;
    }
    HRESULT hr = ctx->device()->CreatePixelShader(
        blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
        entry.target->GetAddressOf());
    if (FAILED(hr)) {
      qCritical() << "RadialRenderer: CreatePixelShader failed for"
                  << entry.file << ":" << Qt::hex << hr;
      return false;
    }
  }

  qInfo() << "RadialRenderer: All shaders compiled successfully";
  return true;
}

// ============================================================================
// Constant Buffers
// ============================================================================

bool RadialRenderer::createConstantBuffers(D3D11Context *ctx) {
  auto createCB = [&](UINT size, ComPtr<ID3D11Buffer> &buffer) -> bool {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = size;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = ctx->device()->CreateBuffer(&desc, nullptr,
                                              buffer.GetAddressOf());
    return SUCCEEDED(hr);
  };

  if (!createCB(sizeof(QuadCBData), m_quadCB)) {
    qCritical() << "RadialRenderer: Failed to create quad CB";
    return false;
  }
  if (!createCB(sizeof(WheelCBData), m_wheelCB)) {
    qCritical() << "RadialRenderer: Failed to create wheel CB";
    return false;
  }
  if (!createCB(sizeof(ElementCBData), m_elemCB)) {
    qCritical() << "RadialRenderer: Failed to create element CB";
    return false;
  }

  return true;
}

// ============================================================================
// Background Texture (Procedural Placeholder)
// ============================================================================

bool RadialRenderer::createBackgroundTexture(D3D11Context *ctx) {
  // Generate a dark circular gradient background (placeholder)
  // Matches GW2Radial's dark textured ring aesthetic
  constexpr int kSize = 256;
  QImage bgImage(kSize, kSize, QImage::Format_RGBA8888);

  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      float fx = (x - kSize * 0.5f) / (kSize * 0.5f);
      float fy = (y - kSize * 0.5f) / (kSize * 0.5f);
      float r = qSqrt(fx * fx + fy * fy);

      // Dark center, slightly brighter ring area, fade at edges
      float brightness = 0.0f;
      if (r < 1.0f) {
        // Ring region: subtle radial gradient
        brightness = 0.08f + 0.04f * (1.0f - qAbs(r - 0.5f) * 2.0f);
        // Add subtle noise-like variation from position
        float noise = qSin(fx * 37.0f) * qCos(fy * 41.0f) * 0.02f;
        brightness += noise;
        brightness = qBound(0.0f, brightness, 1.0f);
      }

      int gray = static_cast<int>(brightness * 255.0f);
      int alpha = (r < 1.0f) ? 255 : 0;
      bgImage.setPixelColor(x, y, QColor(gray, gray, gray, alpha));
    }
  }

  m_backgroundSRV = ctx->createTextureFromRGBA(kSize, kSize,
                                                bgImage.constBits());
  if (!m_backgroundSRV) {
    qCritical() << "RadialRenderer: Failed to create background texture";
    return false;
  }

  qInfo() << "RadialRenderer: Placeholder background texture created"
          << kSize << "x" << kSize;
  return true;
}

// ============================================================================
// Constant Buffer Updates
// ============================================================================

void RadialRenderer::updateQuadCB(D3D11Context *ctx, float centerX,
                                   float centerY, float halfW, float halfH,
                                   bool tilt, int mouseX, int mouseY,
                                   int screenW, int screenH) {
  auto *dc = ctx->context();

  D3D11_MAPPED_SUBRESOURCE mapped;
  HRESULT hr = dc->Map(m_quadCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    return;
  }

  auto *cb = static_cast<QuadCBData *>(mapped.pData);
  memset(cb, 0, sizeof(QuadCBData));

  cb->spriteDimensions[0] = centerX;
  cb->spriteDimensions[1] = centerY;
  cb->spriteDimensions[2] = halfW;
  cb->spriteDimensions[3] = halfH;
  cb->spriteZ = 0.0f;

  // Identity tilt matrix (column-major 4x4)
  // [1 0 0 0] [0 1 0 0] [0 0 1 0] [0 0 0 1]
  cb->tiltMatrix[0] = 1.0f;
  cb->tiltMatrix[5] = 1.0f;
  cb->tiltMatrix[10] = 1.0f;
  cb->tiltMatrix[15] = 1.0f;

  Q_UNUSED(tilt);
  Q_UNUSED(mouseX);
  Q_UNUSED(mouseY);
  Q_UNUSED(screenW);
  Q_UNUSED(screenH);

  dc->Unmap(m_quadCB.Get(), 0);
}

void RadialRenderer::updateWheelCB(D3D11Context *ctx, RadialWheel *wheel) {
  auto *dc = ctx->context();

  D3D11_MAPPED_SUBRESOURCE mapped;
  HRESULT hr =
      dc->Map(m_wheelCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    return;
  }

  auto *cb = static_cast<WheelCBData *>(mapped.pData);
  memset(cb, 0, sizeof(WheelCBData));

  cb->wipeMaskData[0] = 0.0f;
  cb->wipeMaskData[1] = 0.0f;
  cb->wipeMaskData[2] = 0.0f;
  cb->wheelFadeIn = wheel->wheelFadeIn();
  cb->animationTimer = wheel->animationTimer();
  cb->centerScale = wheel->centerScale();
  cb->elementCount = wheel->visibleElements().size();
  cb->globalOpacity = wheel->globalOpacity();
  cb->timeLeft = 0.0f;
  cb->showIcon = 0;

  wheel->fillHoverFadeIns(cb->hoverFadeIns, kMaxElements);

  dc->Unmap(m_wheelCB.Get(), 0);
}

void RadialRenderer::updateElementCB(D3D11Context *ctx,
                                      RadialElement *element) {
  auto *dc = ctx->context();

  D3D11_MAPPED_SUBRESOURCE mapped;
  HRESULT hr =
      dc->Map(m_elemCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    return;
  }

  auto *cb = static_cast<ElementCBData *>(mapped.pData);
  memset(cb, 0, sizeof(ElementCBData));

  cb->adjustedColor[0] = element->colorR;
  cb->adjustedColor[1] = element->colorG;
  cb->adjustedColor[2] = element->colorB;
  cb->adjustedColor[3] = element->colorA;
  cb->elementHoverFadeIn = element->hoverFadeIn;
  cb->premultiplyAlpha = element->premultipliedAlpha ? 1 : 0;

  dc->Unmap(m_elemCB.Get(), 0);
}

// ============================================================================
// Draw Methods
// ============================================================================

void RadialRenderer::drawWheel(D3D11Context *ctx, RadialWheel *wheel) {
  if (!m_initialized || !ctx->isInitialized()) {
    return;
  }

  auto *dc = ctx->context();
  float screenW = static_cast<float>(ctx->width());
  float screenH = static_cast<float>(ctx->height());

  // Wheel quad: centered on screen, aspect-ratio-corrected square
  float scale = m_wheelScale; // Configurable via setWheelScale()
  float halfW = scale * 0.5f * screenH / screenW; // Aspect correction
  float halfH = scale * 0.5f;

  updateQuadCB(ctx, wheel->centerX(), wheel->centerY(), halfW, halfH, false, 0,
               0, static_cast<int>(screenW), static_cast<int>(screenH));
  updateWheelCB(ctx, wheel);

  // Set pipeline
  dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  dc->VSSetShader(m_radialVS.Get(), nullptr, 0);
  dc->PSSetShader(m_wheelPS.Get(), nullptr, 0);

  ID3D11Buffer *vsCBs[] = {m_quadCB.Get()};
  dc->VSSetConstantBuffers(0, 1, vsCBs);

  ID3D11Buffer *psCBs[] = {m_wheelCB.Get()};
  dc->PSSetConstantBuffers(0, 1, psCBs);

  ID3D11ShaderResourceView *srvs[] = {m_backgroundSRV.Get()};
  dc->PSSetShaderResources(0, 1, srvs);

  ID3D11SamplerState *samplers[] = {m_sampler.Get(), m_sampler.Get()};
  dc->PSSetSamplers(0, 2, samplers);

  float blendFactor[4] = {0, 0, 0, 0};
  dc->OMSetBlendState(ctx->alphaBlendState(), blendFactor, 0xFFFFFFFF);

  D3D11_VIEWPORT vp = {};
  vp.Width = screenW;
  vp.Height = screenH;
  vp.MaxDepth = 1.0f;
  dc->RSSetViewports(1, &vp);

  ID3D11RenderTargetView *rtv = ctx->renderTargetView();
  dc->OMSetRenderTargets(1, &rtv, nullptr);

  dc->Draw(4, 0);
}

void RadialRenderer::drawElement(D3D11Context *ctx, RadialWheel *wheel,
                                  RadialElement *element, int index,
                                  int totalElements) {
  if (!m_initialized || !ctx->isInitialized()) {
    return;
  }

  auto *dc = ctx->context();
  float screenW = static_cast<float>(ctx->width());
  float screenH = static_cast<float>(ctx->height());

  // Element quad: positioned within its angular slice
  // Calculate the angle for this element's center
  float angleStep =
      static_cast<float>(M_PI) * 2.0f / static_cast<float>(totalElements);
  float angle =
      static_cast<float>(index) * angleStep - static_cast<float>(M_PI) * 0.5f;

  // Position element icon — match GW2Radial: elementLocation * 0.2 of wheel half-size
  // GW2Radial WheelElement.cpp line 224:
  //   elementLocation = {cos(angle) * 0.2, sin(angle) * 0.2}
  //   spriteDimensions.x += elementLocation.x * spriteDimensions.z
  //   spriteDimensions.y += elementLocation.y * spriteDimensions.w
  float wheelScale = m_wheelScale;
  float wheelHalfW = wheelScale * 0.5f * screenH / screenW;
  float wheelHalfH = wheelScale * 0.5f;

  float elemLocationX = qCos(angle) * 0.2f;
  float elemLocationY = qSin(angle) * 0.2f;

  float elemCenterX = wheel->centerX() + elemLocationX * wheelHalfW;
  float elemCenterY = wheel->centerY() + elemLocationY * wheelHalfH;

  // Element icon size — GW2Radial uses elementDiameter based on element count
  // For 9 elements: sin(2PI/9/2) * 2 * 0.2 * 0.66 ≈ 0.09
  float elementDiameter =
      static_cast<float>(qSin(M_PI / totalElements)) * 2.0f * 0.2f * 0.66f;
  float halfW = wheelHalfW * elementDiameter;
  float halfH = wheelHalfH * elementDiameter;

  updateQuadCB(ctx, elemCenterX, elemCenterY, halfW, halfH, false, 0, 0,
               static_cast<int>(screenW), static_cast<int>(screenH));
  updateElementCB(ctx, element);

  dc->VSSetShader(m_radialVS.Get(), nullptr, 0);
  dc->PSSetShader(m_elementPS.Get(), nullptr, 0);

  ID3D11Buffer *vsCBs[] = {m_quadCB.Get()};
  dc->VSSetConstantBuffers(0, 1, vsCBs);

  // Both CBs needed: wheel CB for wheelFadeIn/globalOpacity, element CB for hover
  ID3D11Buffer *psCBs[] = {m_wheelCB.Get(), m_elemCB.Get()};
  dc->PSSetConstantBuffers(0, 2, psCBs);

  // Bind element icon texture (or default white if no icon loaded)
  if (element->iconSRV) {
    ID3D11ShaderResourceView *srvs[] = {nullptr, element->iconSRV.Get()};
    dc->PSSetShaderResources(0, 2, srvs);
  }

  dc->Draw(4, 0);
}

void RadialRenderer::drawCursor(D3D11Context *ctx, int mouseX, int mouseY,
                                 int screenW, int screenH,
                                 float animationTimer, float globalOpacity) {
  if (!m_initialized || !ctx->isInitialized()) {
    return;
  }

  auto *dc = ctx->context();
  float sw = static_cast<float>(screenW);
  float sh = static_cast<float>(screenH);

  // Cursor quad: small circle at mouse position
  float cursorSize = 0.08f; // 8% of screen height
  float cx = static_cast<float>(mouseX) / sw;
  float cy = static_cast<float>(mouseY) / sh;

  updateQuadCB(ctx, cx, cy, cursorSize * sh / sw, cursorSize, false, mouseX,
               mouseY, screenW, screenH);

  // Update wheel CB with animation timer for cursor noise
  {
    auto *devCtx = ctx->context();
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = devCtx->Map(m_wheelCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                              &mapped);
    if (SUCCEEDED(hr)) {
      auto *cb = static_cast<WheelCBData *>(mapped.pData);
      memset(cb, 0, sizeof(WheelCBData));
      cb->animationTimer = animationTimer;
      cb->globalOpacity = globalOpacity;
      devCtx->Unmap(m_wheelCB.Get(), 0);
    }
  }

  dc->VSSetShader(m_radialVS.Get(), nullptr, 0);
  dc->PSSetShader(m_cursorPS.Get(), nullptr, 0);

  ID3D11Buffer *vsCBs[] = {m_quadCB.Get()};
  dc->VSSetConstantBuffers(0, 1, vsCBs);

  ID3D11Buffer *psCBs[] = {m_wheelCB.Get()};
  dc->PSSetConstantBuffers(0, 1, psCBs);

  dc->Draw(4, 0);
}

/**
 * @file SpriteBatch.cpp
 * @brief Batched 2D sprite rendering implementation
 *
 * Collects textured quads, sorts by texture, and submits in batched
 * draw calls for GPU efficiency. Uses the sprite.hlsl shader.
 *
 * Vertex format: position (2D pixels), texcoord (UV), color (RGBA
 * premultiplied) Index buffer: static quad topology (0,1,2, 2,3,0 repeated)
 *
 * Inspired by MonoGame's SpriteBatch — the same pattern used by Blish HUD
 * for all 2D UI rendering in its overlay.
 */

#include "SpriteBatch.h"
#include "D3D11Context.h"

#include <QDebug>
#include <QFile>

#include <algorithm>

// Embedded shader source (loaded from file at runtime, or embedded here)
// We'll load from the shaders/ directory at initialization time.

// ============================================================================
// Constructor / Destructor
// ============================================================================

SpriteBatch::SpriteBatch(D3D11Context *context) : m_context(context) {
  m_batchItems.reserve(256);
}

SpriteBatch::~SpriteBatch() = default;

// ============================================================================
// Initialization
// ============================================================================

bool SpriteBatch::initialize() {
  if (!m_context || !m_context->isInitialized()) {
    qWarning() << "SpriteBatch: D3D11Context not initialized";
    return false;
  }

  // --- Load and compile sprite shader ---

  QFile shaderFile(":/shaders/sprite.hlsl");
  QByteArray shaderSource;

  if (shaderFile.open(QIODevice::ReadOnly)) {
    shaderSource = shaderFile.readAll();
    shaderFile.close();
  } else {
    // Fallback: try filesystem path relative to executable
    QFile localFile("shaders/sprite.hlsl");
    if (localFile.open(QIODevice::ReadOnly)) {
      shaderSource = localFile.readAll();
      localFile.close();
    } else {
      qCritical() << "SpriteBatch: Could not load sprite.hlsl";
      return false;
    }
  }

  // Compile vertex shader
  QString vsError;
  auto vsBlob =
      m_context->compileShader(shaderSource, "VSMain", "vs_5_0", vsError);
  if (!vsBlob) {
    qCritical() << "SpriteBatch VS compilation failed:" << vsError;
    return false;
  }

  HRESULT hr = m_context->device()->CreateVertexShader(
      vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
      m_vertexShader.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SpriteBatch: CreateVertexShader failed:" << Qt::hex << hr;
    return false;
  }

  // Compile pixel shader
  QString psError;
  auto psBlob =
      m_context->compileShader(shaderSource, "PSMain", "ps_5_0", psError);
  if (!psBlob) {
    qCritical() << "SpriteBatch PS compilation failed:" << psError;
    return false;
  }

  hr = m_context->device()->CreatePixelShader(psBlob->GetBufferPointer(),
                                              psBlob->GetBufferSize(), nullptr,
                                              m_pixelShader.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SpriteBatch: CreatePixelShader failed:" << Qt::hex << hr;
    return false;
  }

  // --- Input layout ---

  D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  hr = m_context->device()->CreateInputLayout(
      layoutDesc, _countof(layoutDesc), vsBlob->GetBufferPointer(),
      vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SpriteBatch: CreateInputLayout failed:" << Qt::hex << hr;
    return false;
  }

  // --- Vertex buffer (dynamic) ---

  D3D11_BUFFER_DESC vbDesc = {};
  vbDesc.ByteWidth = static_cast<UINT>(kMaxSprites * kVerticesPerSprite *
                                       sizeof(SpriteVertex));
  vbDesc.Usage = D3D11_USAGE_DYNAMIC;
  vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = m_context->device()->CreateBuffer(&vbDesc, nullptr,
                                         m_vertexBuffer.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SpriteBatch: CreateBuffer (VB) failed:" << Qt::hex << hr;
    return false;
  }

  // --- Index buffer (static quad topology) ---

  QVector<uint16_t> indices;
  indices.resize(kMaxSprites * kIndicesPerSprite);
  for (int i = 0; i < kMaxSprites; i++) {
    int base = i * 4;
    int idx = i * 6;
    indices[idx + 0] = static_cast<uint16_t>(base + 0);
    indices[idx + 1] = static_cast<uint16_t>(base + 1);
    indices[idx + 2] = static_cast<uint16_t>(base + 2);
    indices[idx + 3] = static_cast<uint16_t>(base + 2);
    indices[idx + 4] = static_cast<uint16_t>(base + 3);
    indices[idx + 5] = static_cast<uint16_t>(base + 0);
  }

  D3D11_BUFFER_DESC ibDesc = {};
  ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
  ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
  ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

  D3D11_SUBRESOURCE_DATA ibData = {};
  ibData.pSysMem = indices.constData();

  hr = m_context->device()->CreateBuffer(&ibDesc, &ibData,
                                         m_indexBuffer.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SpriteBatch: CreateBuffer (IB) failed:" << Qt::hex << hr;
    return false;
  }

  // --- Constant buffer ---

  D3D11_BUFFER_DESC cbDesc = {};
  cbDesc.ByteWidth = 16; // float2 ScreenSize + float2 padding
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = m_context->device()->CreateBuffer(&cbDesc, nullptr,
                                         m_constantBuffer.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SpriteBatch: CreateBuffer (CB) failed:" << Qt::hex << hr;
    return false;
  }

  // --- Default white texture ---
  m_whiteTexture = m_context->createDefaultWhiteTexture();
  if (!m_whiteTexture) {
    qCritical() << "SpriteBatch: Failed to create white texture";
    return false;
  }

  // --- Sampler ---
  m_sampler = m_context->createLinearSampler();
  if (!m_sampler) {
    qCritical() << "SpriteBatch: Failed to create sampler";
    return false;
  }

  m_initialized = true;
  qInfo() << "SpriteBatch initialized (max" << kMaxSprites << "sprites)";
  return true;
}

// ============================================================================
// Begin / End
// ============================================================================

void SpriteBatch::begin() {
  if (m_inBatch) {
    qWarning() << "SpriteBatch::begin() called while already in batch";
    return;
  }
  m_batchItems.clear();
  m_inBatch = true;
}

void SpriteBatch::end() {
  if (!m_inBatch) {
    qWarning() << "SpriteBatch::end() called without begin()";
    return;
  }
  m_inBatch = false;

  if (m_batchItems.isEmpty()) {
    return;
  }

  flush();
}

// ============================================================================
// Drawing Primitives
// ============================================================================

void SpriteBatch::drawRect(float x, float y, float width, float height,
                           const QColor &color) {
  drawTexture(nullptr, x, y, width, height, color);
}

void SpriteBatch::drawRect(const QRectF &rect, const QColor &color) {
  drawRect(static_cast<float>(rect.x()), static_cast<float>(rect.y()),
           static_cast<float>(rect.width()), static_cast<float>(rect.height()),
           color);
}

void SpriteBatch::drawTexture(ID3D11ShaderResourceView *texture, float x,
                              float y, float width, float height,
                              const QColor &tint) {
  drawTexture(texture, x, y, width, height, QRectF(0, 0, 1, 1), tint);
}

void SpriteBatch::drawTexture(ID3D11ShaderResourceView *texture, float x,
                              float y, float width, float height,
                              const QRectF &srcRect, const QColor &tint) {
  if (!m_inBatch || !m_initialized) {
    return;
  }

  if (m_batchItems.size() >= kMaxSprites) {
    // Flush current batch and continue
    flush();
    m_batchItems.clear();
  }

  // Use white texture for untextured rects
  ID3D11ShaderResourceView *tex = texture ? texture : m_whiteTexture.Get();

  float r = static_cast<float>(tint.redF());
  float g = static_cast<float>(tint.greenF());
  float b = static_cast<float>(tint.blueF());
  float a = static_cast<float>(tint.alphaF());

  float u0 = static_cast<float>(srcRect.left());
  float v0 = static_cast<float>(srcRect.top());
  float u1 = static_cast<float>(srcRect.right());
  float v1 = static_cast<float>(srcRect.bottom());

  SpriteBatchItem item;
  item.texture = tex;

  // Quad corners: top-left, top-right, bottom-right, bottom-left
  item.vertices[0] = {x, y, u0, v0, r, g, b, a};
  item.vertices[1] = {x + width, y, u1, v0, r, g, b, a};
  item.vertices[2] = {x + width, y + height, u1, v1, r, g, b, a};
  item.vertices[3] = {x, y + height, u0, v1, r, g, b, a};

  m_batchItems.append(item);
}

void SpriteBatch::drawTextureAlpha(ID3D11ShaderResourceView *texture, float x,
                                   float y, float width, float height,
                                   float alpha) {
  QColor tint(255, 255, 255, static_cast<int>(alpha * 255.0f));
  drawTexture(texture, x, y, width, height, tint);
}

// ============================================================================
// Flush — Submit to GPU
// ============================================================================

void SpriteBatch::flush() {
  if (m_batchItems.isEmpty() || !m_initialized) {
    return;
  }

  auto *ctx = m_context->context();

  // Update constant buffer with screen size
  {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                          &mapped);
    if (SUCCEEDED(hr)) {
      float cbData[4] = {static_cast<float>(m_context->width()),
                         static_cast<float>(m_context->height()), 0.0f, 0.0f};
      memcpy(mapped.pData, cbData, sizeof(cbData));
      ctx->Unmap(m_constantBuffer.Get(), 0);
    }
  }

  // Upload all vertices to GPU
  {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr =
        ctx->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
      auto *dest = static_cast<SpriteVertex *>(mapped.pData);
      for (int i = 0; i < m_batchItems.size(); i++) {
        memcpy(dest + i * 4, m_batchItems[i].vertices,
               sizeof(SpriteVertex) * 4);
      }
      ctx->Unmap(m_vertexBuffer.Get(), 0);
    }
  }

  // Set pipeline state
  ctx->IASetInputLayout(m_inputLayout.Get());

  UINT stride = sizeof(SpriteVertex);
  UINT offset = 0;
  ID3D11Buffer *vbs[] = {m_vertexBuffer.Get()};
  ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
  ctx->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  ctx->VSSetShader(m_vertexShader.Get(), nullptr, 0);
  ctx->PSSetShader(m_pixelShader.Get(), nullptr, 0);

  ID3D11Buffer *cbs[] = {m_constantBuffer.Get()};
  ctx->VSSetConstantBuffers(0, 1, cbs);

  ID3D11SamplerState *samplers[] = {m_sampler.Get()};
  ctx->PSSetSamplers(0, 1, samplers);

  // Sort by texture for batching (minimize texture switches)
  std::stable_sort(m_batchItems.begin(), m_batchItems.end(),
                   [](const SpriteBatchItem &a, const SpriteBatchItem &b) {
                     return a.texture < b.texture;
                   });

  // Submit batches
  ID3D11ShaderResourceView *currentTex = nullptr;
  int batchStart = 0;

  for (int i = 0; i < m_batchItems.size(); i++) {
    if (m_batchItems[i].texture != currentTex) {
      if (i > batchStart) {
        submitBatch(currentTex, batchStart, i - batchStart);
      }
      currentTex = m_batchItems[i].texture;
      batchStart = i;
    }
  }

  // Submit final batch
  if (m_batchItems.size() > batchStart) {
    submitBatch(currentTex, batchStart, m_batchItems.size() - batchStart);
  }
}

void SpriteBatch::submitBatch(ID3D11ShaderResourceView *texture, int startIndex,
                              int count) {
  auto *ctx = m_context->context();

  // Bind texture
  ID3D11ShaderResourceView *srvs[] = {texture};
  ctx->PSSetShaderResources(0, 1, srvs);

  // Draw indexed
  ctx->DrawIndexed(static_cast<UINT>(count * kIndicesPerSprite),
                   static_cast<UINT>(startIndex * kIndicesPerSprite), 0);
}

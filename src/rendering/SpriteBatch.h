#pragma once

/**
 * @brief Batched 2D sprite rendering for UI elements
 *
 * Similar to MonoGame's SpriteBatch — collects textured quads and
 * submits them in a single draw call for efficiency.
 *
 * Usage:
 *   spriteBatch.begin();
 *   spriteBatch.drawRect(x, y, w, h, color);
 *   spriteBatch.drawTexture(texture, x, y, w, h, tint);
 *   spriteBatch.end();
 *
 * Features:
 * - Textured quads with tint color
 * - Filled rectangles (using 1x1 white texture)
 * - Automatic batching (single draw call per texture change)
 * - Screen-space coordinates (pixels, origin top-left)
 *
 * Consumers:
 * - OverlayMenuRenderer: menu panels, icons, text backgrounds
 * - MinimapPipeline: 2D positioned markers
 *
 * DO NOT ADD:
 * - 3D rendering (use MarkerPipeline / TrailPipeline)
 * - Font rendering (future: add a FontBatch or extend SpriteBatch)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11.h>
#include <wrl/client.h>

#include <QColor>
#include <QRectF>
#include <QVector>

using Microsoft::WRL::ComPtr;

class D3D11Context;

class SpriteBatch {
public:
  explicit SpriteBatch(D3D11Context *context);
  ~SpriteBatch();

  // Non-copyable
  SpriteBatch(const SpriteBatch &) = delete;
  SpriteBatch &operator=(const SpriteBatch &) = delete;

  /**
   * @brief Initialize GPU resources (shaders, buffers, default texture)
   * @return true on success
   */
  bool initialize();

  /**
   * @brief Begin collecting sprites for a frame
   * Must be paired with end()
   */
  void begin();

  /**
   * @brief Submit all collected sprites as a single draw call batch
   */
  void end();

  // --- Drawing primitives ---

  /**
   * @brief Draw a filled rectangle
   */
  void drawRect(float x, float y, float width, float height,
                const QColor &color);
  void drawRect(const QRectF &rect, const QColor &color);

  /**
   * @brief Draw a textured quad
   * @param texture Shader resource view (may be null → uses white default)
   */
  void drawTexture(ID3D11ShaderResourceView *texture, float x, float y,
                   float width, float height, const QColor &tint = Qt::white);

  /**
   * @brief Draw a textured quad with source rectangle (for texture atlases)
   * @param srcRect Source rectangle in UV coordinates [0,1]
   */
  void drawTexture(ID3D11ShaderResourceView *texture, float x, float y,
                   float width, float height, const QRectF &srcRect,
                   const QColor &tint = Qt::white);

  /**
   * @brief Draw a textured quad with alpha override
   */
  void drawTextureAlpha(ID3D11ShaderResourceView *texture, float x, float y,
                        float width, float height, float alpha);

private:
  struct SpriteVertex {
    float x, y;       // Screen position (pixels)
    float u, v;       // Texture coordinates
    float r, g, b, a; // Vertex color (premultiplied alpha)
  };

  struct SpriteBatchItem {
    ID3D11ShaderResourceView *texture; // Raw ptr, not owned
    SpriteVertex vertices[4];          // Quad corners
  };

  void flush();
  void submitBatch(ID3D11ShaderResourceView *texture, int startIndex,
                   int count);

  D3D11Context *m_context;

  // Shaders
  ComPtr<ID3D11VertexShader> m_vertexShader;
  ComPtr<ID3D11PixelShader> m_pixelShader;
  ComPtr<ID3D11InputLayout> m_inputLayout;

  // Constant buffer
  ComPtr<ID3D11Buffer> m_constantBuffer;

  // Dynamic vertex buffer
  ComPtr<ID3D11Buffer> m_vertexBuffer;
  static constexpr int kMaxSprites = 4096;
  static constexpr int kVerticesPerSprite = 4;
  static constexpr int kIndicesPerSprite = 6;

  // Index buffer (static — quad topology never changes)
  ComPtr<ID3D11Buffer> m_indexBuffer;

  // Default 1x1 white texture (for untextured rects)
  ComPtr<ID3D11ShaderResourceView> m_whiteTexture;

  // Sampler
  ComPtr<ID3D11SamplerState> m_sampler;

  // Batch state
  QVector<SpriteBatchItem> m_batchItems;
  bool m_inBatch = false;
  bool m_initialized = false;
};

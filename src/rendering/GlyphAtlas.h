#pragma once

/**
 * @brief Glyph atlas for D3D11 text rendering
 *
 * Pre-renders ASCII characters into a single D3D11 texture atlas at startup.
 * Runtime text drawing decomposes strings into per-character SpriteBatch quads,
 * each sampling the atlas at the glyph's UV region.
 *
 * This is the same approach TacO (CWBFont) and Blish HUD (SpriteFont) use.
 * Built once, used for all overlay text rendering.
 *
 * Build phase (QPainter, runs once):
 *   1. Enumerate ASCII printable characters (32–126)
 *   2. Render each glyph with QPainter + dark outline for readability
 *   3. Pack into a single atlas QImage (row-based)
 *   4. Upload to D3D11 SRV via D3D11Context::createTextureFromRGBA()
 *
 * Runtime phase (SpriteBatch, per-frame):
 *   drawString() maps each character to a UV rect and submits SpriteBatch
 * quads.
 *
 * Consumers:
 * - MarkerPipeline: distance labels below markers
 * - Future: OverlayMenuRenderer, MinimapPipeline
 *
 * DO NOT ADD:
 * - Rendering logic beyond text (belongs in pipelines)
 * - Complex layout/wrapping (add later if needed)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11.h>
#include <wrl/client.h>

#include <QColor>
#include <QHash>
#include <QRectF>
#include <QString>

using Microsoft::WRL::ComPtr;

class D3D11Context;
class SpriteBatch;

class GlyphAtlas {
public:
  explicit GlyphAtlas(D3D11Context *context);
  ~GlyphAtlas();

  // Non-copyable
  GlyphAtlas(const GlyphAtlas &) = delete;
  GlyphAtlas &operator=(const GlyphAtlas &) = delete;

  /**
   * @brief Build the atlas from a font
   *
   * Renders ASCII 32-126 with QPainter, packs into atlas, uploads to GPU.
   * Must be called before any drawing. Can be called again to rebuild
   * with a different font.
   *
   * @param fontFamily Font family name (e.g., "Segoe UI")
   * @param fontSize Font size in pixels
   * @param bold Whether to use bold weight
   * @return true if atlas was built and uploaded successfully
   */
  bool build(const QString &fontFamily, int fontSize, bool bold = false);

  /**
   * @brief Measure the pixel width of a text string
   *
   * Sums advance widths for each character. Used for centering text.
   * @return Total pixel width
   */
  float measureText(const QString &text) const;

  /**
   * @brief Get the line height of the built font
   * @return Line height in pixels
   */
  float lineHeight() const { return m_lineHeight; }

  /**
   * @brief Whether the atlas has been built successfully
   */
  bool isBuilt() const { return m_built; }

  /**
   * @brief Get current font size (for dynamic rebuild detection)
   */
  int currentFontSize() const { return m_fontSize; }

  /**
   * @brief Draw a text string using SpriteBatch
   *
   * Must be called between SpriteBatch::begin() and SpriteBatch::end().
   * Each character becomes one SpriteBatch quad sampling the atlas.
   *
   * @param batch SpriteBatch to submit quads to (must be in begin() state)
   * @param text Text string to draw
   * @param x Left edge X position (screen pixels)
   * @param y Top edge Y position (screen pixels)
   * @param color Text color with alpha
   */
  void drawString(SpriteBatch *batch, const QString &text, float x, float y,
                  const QColor &color = Qt::white) const;

private:
  /**
   * @brief Per-glyph metrics and atlas position
   */
  struct GlyphInfo {
    QRectF uvRect;  ///< UV coordinates in atlas [0,1] range
    float advanceX; ///< Horizontal advance to next character (pixels)
    float width;    ///< Glyph render width (pixels)
    float height;   ///< Glyph render height (pixels)
    float bearingX; ///< Horizontal offset from cursor to glyph left edge
    float bearingY; ///< Vertical offset from baseline to glyph top
  };

  D3D11Context *m_context;

  /// Atlas texture on GPU
  ComPtr<ID3D11ShaderResourceView> m_atlasSRV;

  /// Per-character glyph info (ASCII 32-126)
  QHash<QChar, GlyphInfo> m_glyphs;

  /// Font metrics
  float m_lineHeight = 0;
  float m_ascent = 0;

  /// Atlas dimensions
  int m_atlasWidth = 0;
  int m_atlasHeight = 0;

  bool m_built = false;
  int m_fontSize = 0;
};

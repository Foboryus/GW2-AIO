/**
 * @file GlyphAtlas.cpp
 * @brief Glyph atlas implementation — QPainter-based atlas generation +
 * SpriteBatch text drawing
 *
 * Build phase:
 *   1. Create QFont from specified family/size/weight
 *   2. Measure all ASCII printable characters (32-126) using QFontMetricsF
 *   3. Pack glyphs into rows in a single QImage (with 1px padding)
 *   4. Render each glyph with dark outline for readability on any background
 *   5. Upload final QImage to D3D11 SRV via createTextureFromRGBA()
 *
 * Runtime:
 *   drawString() iterates characters, looks up GlyphInfo, submits UV-mapped
 *   SpriteBatch quads from the atlas texture.
 */

#include "GlyphAtlas.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include "D3D11Context.h"
#include "SpriteBatch.h"

#include <QDebug>

// Padding between glyphs in the atlas (pixels)
static constexpr int kGlyphPadding = 2;

// Outline thickness for readability (pixels)
static constexpr float kOutlineWidth = 2.0f;

GlyphAtlas::GlyphAtlas(D3D11Context *context) : m_context(context) {}

GlyphAtlas::~GlyphAtlas() = default;

bool GlyphAtlas::build(const QString &fontFamily, int fontSize, bool bold) {
  // Clear any previous atlas
  m_glyphs.clear();
  m_atlasSRV.Reset();
  m_built = false;

  // Create font
  QFont font(fontFamily, fontSize);
  font.setBold(bold);
  font.setHintingPreference(QFont::PreferFullHinting);
  QFontMetricsF metrics(font);

  m_lineHeight = static_cast<float>(metrics.height());
  m_ascent = static_cast<float>(metrics.ascent());

  // First pass: measure all glyphs to determine atlas dimensions
  struct GlyphMeasure {
    QChar ch;
    float advanceX;
    float width;  // Rendered width including outline
    float height; // Rendered height including outline
  };

  QVector<GlyphMeasure> measures;
  float totalWidth = 0;
  float maxHeight = 0;

  // Extra space for outline on each side
  const float outlineExtra = kOutlineWidth * 2.0f;

  for (int c = 32; c <= 126; ++c) {
    QChar ch(c);
    GlyphMeasure gm;
    gm.ch = ch;
    gm.advanceX = static_cast<float>(metrics.horizontalAdvance(ch));

    // Bounding rect for the glyph (may be wider than advance for italic, etc.)
    QRectF br = metrics.boundingRect(ch);
    gm.width =
        static_cast<float>(qMax(br.width(), static_cast<qreal>(gm.advanceX))) +
        outlineExtra;
    gm.height = static_cast<float>(metrics.height()) + outlineExtra;

    measures.append(gm);
    totalWidth += gm.width + kGlyphPadding;
    maxHeight = qMax(maxHeight, gm.height);
  }

  // Determine atlas dimensions (aim for roughly square)
  // Use a row-based packing: fit glyphs in rows up to maxRowWidth
  int maxRowWidth = qMax(256, static_cast<int>(qSqrt(totalWidth * maxHeight)));
  // Round up to power of 2 for GPU friendliness
  maxRowWidth = qMin(2048, maxRowWidth);

  // Second pass: pack glyphs into rows
  int atlasWidth = 0;
  int atlasHeight = 0;
  {
    int curX = kGlyphPadding;
    int curY = kGlyphPadding;
    int rowHeight = static_cast<int>(maxHeight) + kGlyphPadding;

    for (const auto &gm : measures) {
      int glyphW = static_cast<int>(qCeil(gm.width)) + kGlyphPadding;

      if (curX + glyphW > maxRowWidth) {
        // New row
        atlasWidth = qMax(atlasWidth, curX);
        curX = kGlyphPadding;
        curY += rowHeight;
      }
      curX += glyphW;
    }
    atlasWidth = qMax(atlasWidth, curX);
    atlasHeight = curY + rowHeight + kGlyphPadding;
  }

  // Clamp to reasonable size
  if (atlasWidth <= 0 || atlasHeight <= 0) {
    qWarning() << "GlyphAtlas: degenerate atlas dimensions";
    return false;
  }

  // Create the atlas image (RGBA, transparent background)
  QImage atlas(atlasWidth, atlasHeight, QImage::Format_RGBA8888);
  atlas.fill(Qt::transparent);

  // Third pass: render glyphs into the atlas
  QPainter painter(&atlas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setFont(font);

  int curX = kGlyphPadding;
  int curY = kGlyphPadding;
  int rowHeight = static_cast<int>(maxHeight) + kGlyphPadding;

  for (const auto &gm : measures) {
    int glyphW = static_cast<int>(qCeil(gm.width)) + kGlyphPadding;

    if (curX + glyphW > maxRowWidth) {
      curX = kGlyphPadding;
      curY += rowHeight;
    }

    // Position: draw at baseline offset within the glyph cell
    float drawX = static_cast<float>(curX) + kOutlineWidth;
    float drawY = static_cast<float>(curY) + kOutlineWidth + m_ascent;

    // Draw dark outline using QPainterPath for readability
    QPainterPath path;
    path.addText(static_cast<qreal>(drawX), static_cast<qreal>(drawY), font,
                 QString(gm.ch));

    // Outline: dark semi-transparent stroke
    painter.setPen(QPen(QColor(0, 0, 0, 200), kOutlineWidth, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    // Fill: white glyph (color is applied at runtime via SpriteBatch tint)
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 255));
    painter.drawPath(path);

    // Store glyph info
    GlyphInfo info;
    info.uvRect = QRectF(static_cast<qreal>(curX) / atlasWidth,
                         static_cast<qreal>(curY) / atlasHeight,
                         static_cast<qreal>(qCeil(gm.width)) / atlasWidth,
                         static_cast<qreal>(qCeil(gm.height)) / atlasHeight);
    info.advanceX = gm.advanceX;
    info.width = static_cast<float>(qCeil(gm.width));
    info.height = static_cast<float>(qCeil(gm.height));
    info.bearingX = 0; // Simplified — outline absorbs bearing offset
    info.bearingY = 0;

    m_glyphs.insert(gm.ch, info);
    curX += glyphW;
  }

  painter.end();

  m_atlasWidth = atlasWidth;
  m_atlasHeight = atlasHeight;

  // Upload to GPU
  m_atlasSRV = m_context->createTextureFromRGBA(atlasWidth, atlasHeight,
                                                atlas.constBits());
  if (!m_atlasSRV) {
    qWarning() << "GlyphAtlas: failed to create atlas texture";
    return false;
  }

  m_built = true;
  m_fontSize = fontSize;
  qInfo() << "GlyphAtlas: built" << m_glyphs.size() << "glyphs," << atlasWidth
          << "x" << atlasHeight << "atlas";
  return true;
}

float GlyphAtlas::measureText(const QString &text) const {
  if (!m_built) {
    return 0;
  }

  float width = 0;
  for (const QChar &ch : text) {
    auto it = m_glyphs.constFind(ch);
    if (it != m_glyphs.constEnd()) {
      width += it->advanceX;
    }
  }
  return width;
}

void GlyphAtlas::drawString(SpriteBatch *batch, const QString &text, float x,
                            float y, const QColor &color) const {
  if (!m_built || !batch || text.isEmpty()) {
    return;
  }

  float cursorX = x;

  for (const QChar &ch : text) {
    auto it = m_glyphs.constFind(ch);
    if (it == m_glyphs.constEnd()) {
      continue; // Skip unknown characters
    }

    const GlyphInfo &glyph = it.value();

    // Skip spaces (no visible glyph, just advance)
    if (ch == QLatin1Char(' ')) {
      cursorX += glyph.advanceX;
      continue;
    }

    // Draw glyph quad from atlas
    batch->drawTexture(m_atlasSRV.Get(), cursorX + glyph.bearingX,
                       y + glyph.bearingY, glyph.width, glyph.height,
                       glyph.uvRect, color);

    cursorX += glyph.advanceX;
  }
}

#pragma once

#include <QObject>
#include <QString>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QOpenGLTexture>
#include <QOpenGLFunctions>

/**
 * @brief Font rendering for OpenGL overlay
 * 
 * Renders text using Qt's font system and caches glyphs as textures.
 */
class FontRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
    
public:
    explicit FontRenderer(QObject* parent = nullptr);
    ~FontRenderer();
    
    /**
     * @brief Initialize OpenGL resources
     */
    bool initialize();
    
    /**
     * @brief Load a font
     * @return Font ID
     */
    int loadFont(const QString& family, int size, bool bold = false, bool italic = false);
    
    /**
     * @brief Set default font
     */
    void setDefaultFont(int fontId) { m_defaultFontId = fontId; }
    
    /**
     * @brief Get text dimensions
     */
    QSize measureText(int fontId, const QString& text);
    QSize measureText(const QString& text) { return measureText(m_defaultFontId, text); }
    
    /**
     * @brief Render text to a texture
     * @return Texture ID and size
     */
    struct TextTexture {
        GLuint textureId;
        int width;
        int height;
    };
    TextTexture renderToTexture(int fontId, const QString& text, const QColor& color);
    
    /**
     * @brief Draw text directly (slower, no caching)
     */
    void drawText(int fontId, const QString& text, float x, float y, 
                  const QColor& color = Qt::white, float scale = 1.0f);
    
    /**
     * @brief Draw text with default font
     */
    void drawText(const QString& text, float x, float y, 
                  const QColor& color = Qt::white) {
        drawText(m_defaultFontId, text, x, y, color);
    }
    
    /**
     * @brief Set projection matrix for drawing
     */
    void setProjection(const QMatrix4x4& projection) { m_projection = projection; }
    
    /**
     * @brief Predefined font styles
     */
    enum class FontStyle {
        Default,
        Title,
        Small,
        Monospace
    };
    int getFontStyle(FontStyle style);
    
private:
    struct FontInfo {
        QFont font;
        QFontMetrics* metrics;
    };
    
    QMap<int, FontInfo> m_fonts;
    int m_nextFontId = 1;
    int m_defaultFontId = -1;
    
    // OpenGL shader for text
    class QOpenGLShaderProgram* m_shader = nullptr;
    QMatrix4x4 m_projection;
    
    // Pre-loaded style fonts
    QMap<FontStyle, int> m_styleFonts;
    
    bool m_initialized = false;
};

// Implementation
inline FontRenderer::FontRenderer(QObject* parent)
    : QObject(parent)
{
}

inline FontRenderer::~FontRenderer()
{
    for (auto& fontInfo : m_fonts) {
        delete fontInfo.metrics;
    }
    delete m_shader;
}

inline bool FontRenderer::initialize()
{
    initializeOpenGLFunctions();
    
    // Create shader
    m_shader = new QOpenGLShaderProgram();
    m_shader->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTexCoord;
        
        uniform mat4 projection;
        uniform vec2 position;
        uniform float scale;
        
        out vec2 TexCoord;
        
        void main() {
            vec2 pos = aPos * scale + position;
            gl_Position = projection * vec4(pos, 0.0, 1.0);
            TexCoord = aTexCoord;
        }
    )");
    m_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        
        uniform sampler2D tex;
        uniform vec4 color;
        
        void main() {
            vec4 sampled = texture(tex, TexCoord);
            FragColor = color * sampled;
        }
    )");
    m_shader->link();
    
    // Load default fonts
    m_styleFonts[FontStyle::Default] = loadFont("Segoe UI", 14);
    m_styleFonts[FontStyle::Title] = loadFont("Segoe UI", 20, true);
    m_styleFonts[FontStyle::Small] = loadFont("Segoe UI", 11);
    m_styleFonts[FontStyle::Monospace] = loadFont("Consolas", 12);
    
    m_defaultFontId = m_styleFonts[FontStyle::Default];
    m_initialized = true;
    
    return true;
}

inline int FontRenderer::loadFont(const QString& family, int size, bool bold, bool italic)
{
    FontInfo info;
    info.font = QFont(family, size);
    info.font.setBold(bold);
    info.font.setItalic(italic);
    info.metrics = new QFontMetrics(info.font);
    
    int id = m_nextFontId++;
    m_fonts[id] = info;
    
    return id;
}

inline QSize FontRenderer::measureText(int fontId, const QString& text)
{
    if (!m_fonts.contains(fontId)) return QSize(0, 0);
    
    const FontInfo& info = m_fonts[fontId];
    return QSize(info.metrics->horizontalAdvance(text), info.metrics->height());
}

inline FontRenderer::TextTexture FontRenderer::renderToTexture(int fontId, const QString& text,
                                                                const QColor& color)
{
    TextTexture result = {0, 0, 0};
    
    if (!m_fonts.contains(fontId)) return result;
    
    const FontInfo& info = m_fonts[fontId];
    QSize size = measureText(fontId, text);
    
    if (size.isEmpty()) return result;
    
    // Render to image
    QImage image(size.width(), size.height(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    
    QPainter painter(&image);
    painter.setFont(info.font);
    painter.setPen(color);
    painter.drawText(0, info.metrics->ascent(), text);
    painter.end();
    
    // Create OpenGL texture
    GLuint texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    
    QImage glImage = image.convertToFormat(QImage::Format_RGBA8888);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glImage.width(), glImage.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, glImage.constBits());
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    result.textureId = texId;
    result.width = size.width();
    result.height = size.height();
    
    return result;
}

inline void FontRenderer::drawText(int fontId, const QString& text, float x, float y,
                                    const QColor& color, float scale)
{
    TextTexture tex = renderToTexture(fontId, text, color);
    if (tex.textureId == 0) return;
    
    m_shader->bind();
    m_shader->setUniformValue("projection", m_projection);
    m_shader->setUniformValue("position", QVector2D(x, y));
    m_shader->setUniformValue("scale", scale);
    m_shader->setUniformValue("color", QVector4D(1, 1, 1, 1));  // Already colored in texture
    
    // Draw quad
    float vertices[] = {
        0, 0, 0, 0,
        float(tex.width), 0, 1, 0,
        float(tex.width), float(tex.height), 1, 1,
        0, float(tex.height), 0, 1
    };
    
    glBindTexture(GL_TEXTURE_2D, tex.textureId);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices + 2);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
    m_shader->release();
    
    // Cleanup
    glDeleteTextures(1, &tex.textureId);
}

inline int FontRenderer::getFontStyle(FontStyle style)
{
    return m_styleFonts.value(style, m_defaultFontId);
}

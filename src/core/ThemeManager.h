#pragma once

/**
 * @file ThemeManager.h
 * @brief Theme engine for Fashion Wars skin system
 *
 * Singleton that manages the active theme, compiles the global QSS template
 * with theme tokens, and provides themed icon access.
 *
 * Architecture: Approach B+ (Template + Property Hybrid)
 * - Layer 1: Global QSS template with {{token}} placeholders
 * - Layer 2: Widgets use setProperty("role", "primary") for differentiation
 * - Layer 3: SVG icon recoloring for theme-aware icons
 *
 * DO NOT ADD:
 * - Inline implementations (use ThemeManager.cpp)
 * - UI code (managers never create UI)
 */

#include <QApplication>
#include <QIcon>
#include <QMap>
#include <QObject>
#include <QString>

#include "ThemeData.h"

class ThemeManager : public QObject {
  Q_OBJECT

public:
  enum class BuiltinTheme {
    ClassicGold,
    Light,
    NavyBlue,
  };
  Q_ENUM(BuiltinTheme)

  static ThemeManager &instance();

  /**
   * @brief Switch to a built-in theme preset
   * Recompiles the QSS template and applies globally via
   * qApp->setStyleSheet().
   */
  void setBuiltinTheme(BuiltinTheme theme);

  /**
   * @brief Load a custom theme from ThemeData (for editor/import)
   * Recompiles and applies the theme.
   */
  void setCustomTheme(const ThemeData &data);

  /**
   * @brief Get the current theme data (read-only access)
   */
  const ThemeData &activeTheme() const { return m_activeTheme; }

  /**
   * @brief Get the current built-in theme enum (Invalid if custom)
   */
  BuiltinTheme currentBuiltinTheme() const { return m_currentBuiltin; }

  /**
   * @brief Get a themed icon by name (e.g., "play", "settings")
   * Returns the icon with colors adjusted to match the active theme.
   * Falls back to the default resource icon if not found.
   */
  QIcon icon(const QString &name) const;

  /**
   * @brief Get the compiled global stylesheet
   * Pre-cached on theme change; calling this is free.
   */
  const QString &compiledStyleSheet() const { return m_compiledQss; }

  /**
   * @brief Convert BuiltinTheme enum to display name
   */
  static QString themeName(BuiltinTheme theme);

  /**
   * @brief Get list of all available built-in themes
   */
  static QList<BuiltinTheme> builtinThemes();

signals:
  /**
   * @brief Emitted after a theme is applied
   * UI can connect to perform any post-theme-switch cleanup.
   */
  void themeChanged();

private:
  ThemeManager() = default;

  /**
   * @brief Compile the master QSS template using current ThemeData tokens
   * Replaces all {{token}} placeholders with actual color values.
   */
  void compileAndApply();

  /**
   * @brief Build the master QSS template string with {{token}} placeholders
   */
  static QString masterTemplate();

  /**
   * @brief Recolor an SVG string by replacing stroke/fill colors
   * @param svgContent  Raw SVG XML text
   * @param oldColor    Color to replace (e.g., "#C09C57")
   * @param newColor    Replacement color from theme
   * @return Modified SVG XML text
   */
  static QString recolorSvg(const QString &svgContent, const QString &oldColor,
                            const QString &newColor);

  /**
   * @brief Rebuild the icon cache with current theme's icon color
   */
  void rebuildIconCache();

  /**
   * @brief Re-apply themed icons to all widgets that have a
   * "themedIconName" property (set by UIHelpers::setThemedIcon /
   * setThemedPixmap). Called after rebuildIconCache().
   */
  void refreshThemedIcons();

  ThemeData m_activeTheme;
  BuiltinTheme m_currentBuiltin = BuiltinTheme::ClassicGold;
  QString m_compiledQss;
  QMap<QString, QIcon> m_iconCache;

  // Default icon color that SVGs ship with (used as replacement source)
  static constexpr const char *kDefaultIconColor = "#C09C57";
};

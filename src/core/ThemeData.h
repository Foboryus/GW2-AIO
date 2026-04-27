#pragma once

/**
 * @file ThemeData.h
 * @brief Theme token data structure for Fashion Wars skin system
 *
 * Holds all visual tokens (colors, layout, animations) for a single theme.
 * Default constructor initializes to "Classic Gold" values (matching current
 * hardcoded UIHelpers values).
 *
 * File format: JSON with "type": "aio_theme", "version": 1
 *
 * DO NOT ADD:
 * - Inline implementations beyond trivial getters (use ThemeData.cpp)
 */

#include <QJsonObject>
#include <QString>

/**
 * @brief Sub-structure for a button's full style tokens
 * Each button role (primary, confirm, cancel, etc.) has these fields.
 */
struct ButtonTokens {
  QString bg;
  QString text;
  QString border;
  QString hoverBg;
  QString hoverBorder;
  QString hoverText; // Empty means inherit from 'text'
  QString pressedBg;

  QJsonObject toJson() const;
  static ButtonTokens fromJson(const QJsonObject &obj,
                               const ButtonTokens &defaults);
};

/**
 * @brief Complete theme data — all visual tokens for the AIO application
 *
 * Organized into categories:
 *   - colors:     Window backgrounds, text, status indicators
 *   - buttons:    Per-role button styles (primary, confirm, cancel, etc.)
 *   - widgets:    GroupBox, input, popup, combobox, scrollbar, etc.
 *   - layout:     Border radius, padding, font sizes
 *   - animations: Glow/gradient effects (Phase 5)
 */
struct ThemeData {
  // ---- Metadata ----
  QString name = "Classic Gold";
  int schemaVersion = 1;

  // ---- Window & Background Colors ----
  struct Colors {
    QString windowBg = "#1A1A1A";
    QString windowSurface = "#2A2A2A"; // Dialogs, popup backgrounds
    QString titleBarBg = "#1A1A1A";
    QString containerBg =
        "#222222";                 // Content area (slightly brighter than nav)
    QString overlayBg = "#1A1A1A"; // Modal overlays

    // Text
    QString textPrimary = "#E0E0E0";
    QString textSecondary = "#888888";
    QString textHint = "#888888";           // Small gray italic text
    QString textAccent = "#C09C5B";         // Gold accent text
    QString textAccentSubtle = "#C09C5B19"; // Gold accent at ~10% (hover tint)

    // Status colors
    QString success = "#2ECC71";
    QString warning = "#F4D03F";
    QString error = "#E74C3C";

    // Icon color for Quick Recolor
    QString iconColor = "#C09C57";

    QJsonObject toJson() const;
    static Colors fromJson(const QJsonObject &obj, const Colors &defaults);
  } colors;

  // ---- Toggle Switch ----
  struct ToggleTokens {
    QString onBg = "#4CD964";        // Green when checked
    QString offBg = "#646464";       // Gray when unchecked
    QString handleColor = "#FFFFFF"; // Circle handle

    QJsonObject toJson() const;
    static ToggleTokens fromJson(const QJsonObject &obj,
                                 const ToggleTokens &defaults);
  } toggle;

  // ---- Buttons ----
  struct Buttons {
    // Primary: Gold border, dark warm bg (Launch, main CTAs)
    ButtonTokens primary = {"#3A3020", "#C09C5B", "#C09C5B", "#4A3A2A",
                            "#D4B06A", "#D4B06A", "#2A2018"};

    // Confirm: Gray base, green hover (OK, Save, Yes)
    ButtonTokens confirm = {"#3A3A3A", "#E0E0E0", "#555555", "#4A8A4A",
                            "#5A9A5A", "",        "#3A7A3A"};

    // Cancel: Gray base, red hover (Cancel, Delete, Exit)
    ButtonTokens cancel = {"#3A3A3A", "#E0E0E0", "#555555", "#8B0000",
                           "#A00000", "#FFFFFF", "#6B0000"};

    // Neutral: Gray base, gold hover (Edit, Browse, misc)
    ButtonTokens neutral = {"#3A3A3A", "#E0E0E0", "#555555", "#5A4A3A",
                            "#C09C5B", "",        "#4A3A2A"};

    // Action: Green bg (urgent actions like Update)
    ButtonTokens action = {"#4A8A4A", "#E0E0E0", "#5A9A5A", "#5A9A5A",
                           "",        "",        "#3A7A3A"};

    // Apply Dirty: Gold highlight for unsaved changes
    ButtonTokens applyDirty = {"#5A4A3A", "#C09C5B", "#C09C5B", "#6A5A4A",
                               "#D4B06A", "#D4B06A", "#4A3A2A"};

    // Apply Clean: Dimmed gray when all saved
    ButtonTokens applyClean = {"#2A2A2A", "#777777", "#3A3A3A", "#3A3A3A",
                               "#555555", "#999999", "#252525"};

    // Nav Active: Currently selected sidebar button
    ButtonTokens navActive = {"#333333", "#C09C57", "transparent", "", "",
                              "",        ""};

    // Nav Inactive: Unselected sidebar button
    ButtonTokens navInactive = {
        "transparent", "#AAAAAA", "transparent", "#2A2A2A", "", "#C09C57", ""};

    // Close: Title bar close button (red hover)
    ButtonTokens close = {"#3A3A3A", "", "#555555", "#8B0000",
                          "#8B0000", "", "#6B0000"};

    // TitleMinimize: Title bar minimize button (warm gold hover)
    ButtonTokens titleMinimize = {"#3A3A3A", "", "#555555", "#5A4A3A",
                                  "#C09C5B", "", "#4A3A2A"};

    // TitleTray: Title bar minimize-to-tray button (blue hover)
    ButtonTokens titleTray = {"#3A3A3A", "", "#555555", "#4A90D9",
                              "#4A90D9", "", "#3A73B0"};

    QJsonObject toJson() const;
    static Buttons fromJson(const QJsonObject &obj, const Buttons &defaults);
  } buttons;

  // ---- Widget Styles ----
  struct Widgets {
    // GroupBox
    QString groupboxBorder = "#444444";
    QString groupboxTitle = "#C09C5B";

    // Input fields (QLineEdit)
    QString inputBg = "#1A1A1A";
    QString inputBorder = "#555555";
    QString inputFocusBorder = "#C09C5B";

    // ComboBox
    QString comboboxBg = "#2A2A2A";
    QString comboboxBorder = "#555555";
    QString comboboxDropdownBg = "#3A3A3A";
    QString comboboxDropdownHover = "#4A4A4A";
    QString comboboxSelectionBg = "#3A3A3A";

    // Popup/Dialog
    QString popupBg = "#2A2A2A";
    QString popupBorder = "#C09C5B";

    // Scrollbar
    QString scrollbarHandle = "#444444";
    QString scrollbarTrack = "#222222";

    // List widget
    QString listBg = "#222222";
    QString listBorder = "#444444";
    QString listSelectedBg = "#C09C5B";
    QString listSelectedText = "#1A1A1A";

    // Tab widget
    QString tabBg = "#2A2A2A";
    QString tabBorder = "#444444";
    QString tabSelectedBg = "#3A3A3A";
    QString tabSelectedBorder = "#C09C5B";
    QString tabText = "#AAAAAA";
    QString tabSelectedText = "#E0E0E0";

    // Slider
    QString sliderGroove = "#333333";
    QString sliderHandle = "#C09C5B";

    // Badge
    QString badgeBg = "#2ECC71";
    QString badgeText = "#FFFFFF";

    // Info banner (Markers tab: missing/failed pack notification)
    QString infoBannerBg = "#252525";
    QString infoBannerBorder = "#555555";
    QString infoBannerText = "#CCCCCC";

    // Pack cards (Online Marker Packs)
    QString packCardBg = "#2A2A2A";          // Card background
    QString packCardBorder = "#444444";      // Card border
    QString packCardHoverBorder = "#C09C5B"; // Card border on hover
    QString userCardBg = "#222222";          // User-installed card (darker)
    QString userCardBorder = "#3A3A3A";      // User-installed card border
    QString packSectionLabel = "#C09C5B";    // "INSTALLED" / "AVAILABLE" label
    QString packSplitter = "#444444";        // Splitter handle color
    QString packProgressBg = "#333333";      // Progress bar track
    QString packProgressFill = "#C09C5B";    // Progress bar fill

    // Page header strip
    QString pageHeaderBg = "#1E1E1E"; // Supports 8-digit hex for alpha
    QString pageHeaderBorder = "#333333";
    QString pageHeaderText; // Empty = falls back to text.accent

    QJsonObject toJson() const;
    static Widgets fromJson(const QJsonObject &obj, const Widgets &defaults);
  } widgets;

  // ---- Overlay (In-Game Menu) ----
  struct OverlayTokens {
    // Panel background and border
    QString panelBg = "#1A1A1AE6";   // Semi-transparent dark (90%)
    QString panelBorder = "#C09C5B"; // Gold border
    QString headerBg = "#252525";    // Slightly lighter header strip

    // Item colors
    QString itemHoverBg = "#333333";   // Row hover
    QString textPrimary = "#E0E0E0";   // Item labels
    QString textSecondary = "#888888"; // Pack subtitle / hint text

    // Toggle indicator colors
    QString toggleOn = "#4CD964";  // Green check
    QString toggleOff = "#555555"; // Gray unchecked

    // Corner icon
    QString iconBg = "#1A1A1ACC";      // Icon background
    QString iconHoverBg = "#333333CC"; // Icon hover background

    // Expander arrow color
    QString expanderColor = "#888888";

    QJsonObject toJson() const;
    static OverlayTokens fromJson(const QJsonObject &obj,
                                  const OverlayTokens &defaults);
  } overlay;

  // ---- Profile Badge Tokens (API badge pills on launcher cards) ----
  struct ProfileBadgeTokens {
    QString pillBg = "transparent";     // Pill background
    QString pillBorder = "#C09C5B";     // Gold border
    QString pillText = "#C09C5B";       // Text color
    QString pillIconColor = "#C09C5B";  // Icon tint
    QString selectedBg = "#3A3020";     // Selection card bg (editor)
    QString selectedBorder = "#C09C5B"; // Selection card border (editor)

    QJsonObject toJson() const;
    static ProfileBadgeTokens fromJson(const QJsonObject &obj,
                                       const ProfileBadgeTokens &defaults);
  } profileBadge;

  // ---- Layout Tokens ----
  struct Layout {
    int borderRadius = 6;
    int popupBorderRadius = 12;
    QString buttonPadding = "8px 20px";
    int fontSizeNormal = 14;
    int fontSizeTitle = 18;
    int fontSizePageTitle = 20;
    int fontSizeHint = 11;
    int fontSizeBadge = 9;
    int pageHeaderHeight = 48;

    // Spacing
    int paddingSmall = 4;   // Compact: hints, notes, sub-items
    int paddingNormal = 8;  // Standard content padding
    int paddingLarge = 12;  // Spacious containers, description panels
    int contentIndent = 24; // Indented list/sub-content

    // Typography
    QString fontFamily = "Segoe UI";                // UI text
    QString fontFamilyMono = "Consolas, monospace"; // Code/args text

    QJsonObject toJson() const;
    static Layout fromJson(const QJsonObject &obj, const Layout &defaults);
  } layout;

  // ---- Animation Tokens (Phase 5) ----
  struct Animations {
    bool enabled = false;
    bool buttonGlow = false;
    QString glowColor = "#C09C5B";
    int glowRadius = 8;
    bool titleBarGradient = false;

    QJsonObject toJson() const;
    static Animations fromJson(const QJsonObject &obj,
                               const Animations &defaults);
  } animations;

  // ---- Serialization ----
  QJsonObject toJson() const;
  static ThemeData fromJson(const QJsonObject &obj);

  // ---- Built-in Presets ----
  static ThemeData classicGold(); // Default — matches current app
  static ThemeData light();
  static ThemeData navyBlue();
};

/**
 * @file ThemeData.cpp
 * @brief Serialization and built-in presets for ThemeData
 *
 * All toJson()/fromJson() methods use default fallbacks for every field,
 * ensuring backward compatibility when loading older theme files.
 *
 * File format: { "type": "aio_theme", "version": 1, ... }
 */

#include "ThemeData.h"

#include <QColor>

// ============================================================================
// Helper: validate hex color, return default if invalid
// ============================================================================
static QString validColor(const QJsonObject &obj, const QString &key,
                          const QString &defaultVal) {
  QString val = obj.value(key).toString(defaultVal);
  if (val.isEmpty() || val == "transparent") {
    return val; // "transparent" and "" are valid special values
  }
  if (QColor::isValidColorName(val)) {
    return val;
  }
  return defaultVal;
}

// ============================================================================
// ButtonTokens
// ============================================================================

QJsonObject ButtonTokens::toJson() const {
  return {{"bg", bg},
          {"text", text},
          {"border", border},
          {"hoverBg", hoverBg},
          {"hoverBorder", hoverBorder},
          {"hoverText", hoverText},
          {"pressedBg", pressedBg}};
}

ButtonTokens ButtonTokens::fromJson(const QJsonObject &obj,
                                    const ButtonTokens &d) {
  ButtonTokens t;
  t.bg = validColor(obj, "bg", d.bg);
  t.text = validColor(obj, "text", d.text);
  t.border = validColor(obj, "border", d.border);
  t.hoverBg = validColor(obj, "hoverBg", d.hoverBg);
  t.hoverBorder = validColor(obj, "hoverBorder", d.hoverBorder);
  t.hoverText = validColor(obj, "hoverText", d.hoverText);
  t.pressedBg = validColor(obj, "pressedBg", d.pressedBg);
  return t;
}

// ============================================================================
// ThemeData::Colors
// ============================================================================

QJsonObject ThemeData::Colors::toJson() const {
  return {{"windowBg", windowBg},
          {"windowSurface", windowSurface},
          {"titleBarBg", titleBarBg},
          {"containerBg", containerBg},
          {"overlayBg", overlayBg},
          {"textPrimary", textPrimary},
          {"textSecondary", textSecondary},
          {"textHint", textHint},
          {"textAccent", textAccent},
          {"textAccentSubtle", textAccentSubtle},
          {"success", success},
          {"warning", warning},
          {"error", error},
          {"iconColor", iconColor}};
}

ThemeData::Colors ThemeData::Colors::fromJson(const QJsonObject &obj,
                                              const Colors &d) {
  Colors c;
  c.windowBg = validColor(obj, "windowBg", d.windowBg);
  c.windowSurface = validColor(obj, "windowSurface", d.windowSurface);
  c.titleBarBg = validColor(obj, "titleBarBg", d.titleBarBg);
  c.containerBg = validColor(obj, "containerBg", d.containerBg);
  c.overlayBg = validColor(obj, "overlayBg", d.overlayBg);
  c.textPrimary = validColor(obj, "textPrimary", d.textPrimary);
  c.textSecondary = validColor(obj, "textSecondary", d.textSecondary);
  c.textHint = validColor(obj, "textHint", d.textHint);
  c.textAccent = validColor(obj, "textAccent", d.textAccent);
  c.textAccentSubtle = validColor(obj, "textAccentSubtle", d.textAccentSubtle);
  c.success = validColor(obj, "success", d.success);
  c.warning = validColor(obj, "warning", d.warning);
  c.error = validColor(obj, "error", d.error);
  c.iconColor = validColor(obj, "iconColor", d.iconColor);
  return c;
}

// ============================================================================
// ThemeData::ToggleTokens
// ============================================================================

QJsonObject ThemeData::ToggleTokens::toJson() const {
  QJsonObject obj;
  obj["onBg"] = onBg;
  obj["offBg"] = offBg;
  obj["handleColor"] = handleColor;
  return obj;
}

ThemeData::ToggleTokens
ThemeData::ToggleTokens::fromJson(const QJsonObject &obj,
                                  const ToggleTokens &d) {
  ToggleTokens t;
  t.onBg = validColor(obj, "onBg", d.onBg);
  t.offBg = validColor(obj, "offBg", d.offBg);
  t.handleColor = validColor(obj, "handleColor", d.handleColor);
  return t;
}

// ============================================================================
// ThemeData::Buttons
// ============================================================================

QJsonObject ThemeData::Buttons::toJson() const {
  return {{"primary", primary.toJson()},
          {"confirm", confirm.toJson()},
          {"cancel", cancel.toJson()},
          {"neutral", neutral.toJson()},
          {"action", action.toJson()},
          {"applyDirty", applyDirty.toJson()},
          {"applyClean", applyClean.toJson()},
          {"navActive", navActive.toJson()},
          {"navInactive", navInactive.toJson()},
          {"close", close.toJson()},
          {"titleMinimize", titleMinimize.toJson()},
          {"titleTray", titleTray.toJson()}};
}

ThemeData::Buttons ThemeData::Buttons::fromJson(const QJsonObject &obj,
                                                const Buttons &d) {
  Buttons b;
  b.primary =
      ButtonTokens::fromJson(obj.value("primary").toObject(), d.primary);
  b.confirm =
      ButtonTokens::fromJson(obj.value("confirm").toObject(), d.confirm);
  b.cancel = ButtonTokens::fromJson(obj.value("cancel").toObject(), d.cancel);
  b.neutral =
      ButtonTokens::fromJson(obj.value("neutral").toObject(), d.neutral);
  b.action = ButtonTokens::fromJson(obj.value("action").toObject(), d.action);
  b.applyDirty =
      ButtonTokens::fromJson(obj.value("applyDirty").toObject(), d.applyDirty);
  b.applyClean =
      ButtonTokens::fromJson(obj.value("applyClean").toObject(), d.applyClean);
  b.navActive =
      ButtonTokens::fromJson(obj.value("navActive").toObject(), d.navActive);
  b.navInactive = ButtonTokens::fromJson(obj.value("navInactive").toObject(),
                                         d.navInactive);
  b.close = ButtonTokens::fromJson(obj.value("close").toObject(), d.close);
  b.titleMinimize = ButtonTokens::fromJson(
      obj.value("titleMinimize").toObject(), d.titleMinimize);
  b.titleTray =
      ButtonTokens::fromJson(obj.value("titleTray").toObject(), d.titleTray);
  return b;
}

// ============================================================================
// ThemeData::Widgets
// ============================================================================

QJsonObject ThemeData::Widgets::toJson() const {
  return {{"groupboxBorder", groupboxBorder},
          {"groupboxTitle", groupboxTitle},
          {"inputBg", inputBg},
          {"inputBorder", inputBorder},
          {"inputFocusBorder", inputFocusBorder},
          {"comboboxBg", comboboxBg},
          {"comboboxBorder", comboboxBorder},
          {"comboboxDropdownBg", comboboxDropdownBg},
          {"comboboxDropdownHover", comboboxDropdownHover},
          {"comboboxSelectionBg", comboboxSelectionBg},
          {"popupBg", popupBg},
          {"popupBorder", popupBorder},
          {"scrollbarHandle", scrollbarHandle},
          {"scrollbarTrack", scrollbarTrack},
          {"listBg", listBg},
          {"listBorder", listBorder},
          {"listSelectedBg", listSelectedBg},
          {"listSelectedText", listSelectedText},
          {"tabBg", tabBg},
          {"tabBorder", tabBorder},
          {"tabSelectedBg", tabSelectedBg},
          {"tabSelectedBorder", tabSelectedBorder},
          {"tabText", tabText},
          {"tabSelectedText", tabSelectedText},
          {"sliderGroove", sliderGroove},
          {"sliderHandle", sliderHandle},
          {"badgeBg", badgeBg},
          {"badgeText", badgeText},
          {"infoBannerBg", infoBannerBg},
          {"infoBannerBorder", infoBannerBorder},
          {"infoBannerText", infoBannerText},
          {"packCardBg", packCardBg},
          {"packCardBorder", packCardBorder},
          {"packCardHoverBorder", packCardHoverBorder},
          {"userCardBg", userCardBg},
          {"userCardBorder", userCardBorder},
          {"packSectionLabel", packSectionLabel},
          {"packSplitter", packSplitter},
          {"packProgressBg", packProgressBg},
          {"packProgressFill", packProgressFill},
          {"pageHeaderBg", pageHeaderBg},
          {"pageHeaderBorder", pageHeaderBorder},
          {"pageHeaderText", pageHeaderText}};
}

ThemeData::Widgets ThemeData::Widgets::fromJson(const QJsonObject &obj,
                                                const Widgets &d) {
  Widgets w;
  w.groupboxBorder = validColor(obj, "groupboxBorder", d.groupboxBorder);
  w.groupboxTitle = validColor(obj, "groupboxTitle", d.groupboxTitle);
  w.inputBg = validColor(obj, "inputBg", d.inputBg);
  w.inputBorder = validColor(obj, "inputBorder", d.inputBorder);
  w.inputFocusBorder = validColor(obj, "inputFocusBorder", d.inputFocusBorder);
  w.comboboxBg = validColor(obj, "comboboxBg", d.comboboxBg);
  w.comboboxBorder = validColor(obj, "comboboxBorder", d.comboboxBorder);
  w.comboboxDropdownBg =
      validColor(obj, "comboboxDropdownBg", d.comboboxDropdownBg);
  w.comboboxDropdownHover =
      validColor(obj, "comboboxDropdownHover", d.comboboxDropdownHover);
  w.comboboxSelectionBg =
      validColor(obj, "comboboxSelectionBg", d.comboboxSelectionBg);
  w.popupBg = validColor(obj, "popupBg", d.popupBg);
  w.popupBorder = validColor(obj, "popupBorder", d.popupBorder);
  w.scrollbarHandle = validColor(obj, "scrollbarHandle", d.scrollbarHandle);
  w.scrollbarTrack = validColor(obj, "scrollbarTrack", d.scrollbarTrack);
  w.listBg = validColor(obj, "listBg", d.listBg);
  w.listBorder = validColor(obj, "listBorder", d.listBorder);
  w.listSelectedBg = validColor(obj, "listSelectedBg", d.listSelectedBg);
  w.listSelectedText = validColor(obj, "listSelectedText", d.listSelectedText);
  w.tabBg = validColor(obj, "tabBg", d.tabBg);
  w.tabBorder = validColor(obj, "tabBorder", d.tabBorder);
  w.tabSelectedBg = validColor(obj, "tabSelectedBg", d.tabSelectedBg);
  w.tabSelectedBorder =
      validColor(obj, "tabSelectedBorder", d.tabSelectedBorder);
  w.tabText = validColor(obj, "tabText", d.tabText);
  w.tabSelectedText = validColor(obj, "tabSelectedText", d.tabSelectedText);
  w.sliderGroove = validColor(obj, "sliderGroove", d.sliderGroove);
  w.sliderHandle = validColor(obj, "sliderHandle", d.sliderHandle);
  w.badgeBg = validColor(obj, "badgeBg", d.badgeBg);
  w.badgeText = validColor(obj, "badgeText", d.badgeText);
  w.infoBannerBg = validColor(obj, "infoBannerBg", d.infoBannerBg);
  w.infoBannerBorder = validColor(obj, "infoBannerBorder", d.infoBannerBorder);
  w.infoBannerText = validColor(obj, "infoBannerText", d.infoBannerText);
  w.packCardBg = validColor(obj, "packCardBg", d.packCardBg);
  w.packCardBorder = validColor(obj, "packCardBorder", d.packCardBorder);
  w.packCardHoverBorder =
      validColor(obj, "packCardHoverBorder", d.packCardHoverBorder);
  w.userCardBg = validColor(obj, "userCardBg", d.userCardBg);
  w.userCardBorder = validColor(obj, "userCardBorder", d.userCardBorder);
  w.packSectionLabel = validColor(obj, "packSectionLabel", d.packSectionLabel);
  w.packSplitter = validColor(obj, "packSplitter", d.packSplitter);
  w.packProgressBg = validColor(obj, "packProgressBg", d.packProgressBg);
  w.packProgressFill = validColor(obj, "packProgressFill", d.packProgressFill);
  w.pageHeaderBg = validColor(obj, "pageHeaderBg", d.pageHeaderBg);
  w.pageHeaderBorder = validColor(obj, "pageHeaderBorder", d.pageHeaderBorder);
  w.pageHeaderText = validColor(obj, "pageHeaderText", d.pageHeaderText);
  return w;
}

// ============================================================================
// ThemeData::OverlayTokens
// ============================================================================

QJsonObject ThemeData::OverlayTokens::toJson() const {
  return {{"panelBg", panelBg},
          {"panelBorder", panelBorder},
          {"headerBg", headerBg},
          {"itemHoverBg", itemHoverBg},
          {"textPrimary", textPrimary},
          {"textSecondary", textSecondary},
          {"toggleOn", toggleOn},
          {"toggleOff", toggleOff},
          {"iconBg", iconBg},
          {"iconHoverBg", iconHoverBg},
          {"expanderColor", expanderColor}};
}

ThemeData::OverlayTokens
ThemeData::OverlayTokens::fromJson(const QJsonObject &obj,
                                   const OverlayTokens &d) {
  OverlayTokens o;
  o.panelBg = validColor(obj, "panelBg", d.panelBg);
  o.panelBorder = validColor(obj, "panelBorder", d.panelBorder);
  o.headerBg = validColor(obj, "headerBg", d.headerBg);
  o.itemHoverBg = validColor(obj, "itemHoverBg", d.itemHoverBg);
  o.textPrimary = validColor(obj, "textPrimary", d.textPrimary);
  o.textSecondary = validColor(obj, "textSecondary", d.textSecondary);
  o.toggleOn = validColor(obj, "toggleOn", d.toggleOn);
  o.toggleOff = validColor(obj, "toggleOff", d.toggleOff);
  o.iconBg = validColor(obj, "iconBg", d.iconBg);
  o.iconHoverBg = validColor(obj, "iconHoverBg", d.iconHoverBg);
  o.expanderColor = validColor(obj, "expanderColor", d.expanderColor);
  return o;
}

// ============================================================================
// ThemeData::Layout
// ============================================================================

QJsonObject ThemeData::Layout::toJson() const {
  return {{"borderRadius", borderRadius},
          {"popupBorderRadius", popupBorderRadius},
          {"buttonPadding", buttonPadding},
          {"fontSizeNormal", fontSizeNormal},
          {"fontSizeTitle", fontSizeTitle},
          {"fontSizePageTitle", fontSizePageTitle},
          {"fontSizeHint", fontSizeHint},
          {"fontSizeBadge", fontSizeBadge},
          {"pageHeaderHeight", pageHeaderHeight},
          {"paddingSmall", paddingSmall},
          {"paddingNormal", paddingNormal},
          {"paddingLarge", paddingLarge},
          {"contentIndent", contentIndent},
          {"fontFamily", fontFamily},
          {"fontFamilyMono", fontFamilyMono}};
}

ThemeData::Layout ThemeData::Layout::fromJson(const QJsonObject &obj,
                                              const Layout &d) {
  Layout l;
  l.borderRadius =
      qBound(0, obj.value("borderRadius").toInt(d.borderRadius), 24);
  l.popupBorderRadius =
      qBound(0, obj.value("popupBorderRadius").toInt(d.popupBorderRadius), 24);
  l.buttonPadding = obj.value("buttonPadding").toString(d.buttonPadding);
  l.fontSizeNormal =
      qBound(6, obj.value("fontSizeNormal").toInt(d.fontSizeNormal), 48);
  l.fontSizeTitle =
      qBound(6, obj.value("fontSizeTitle").toInt(d.fontSizeTitle), 48);
  l.fontSizePageTitle =
      qBound(6, obj.value("fontSizePageTitle").toInt(d.fontSizePageTitle), 48);
  l.fontSizeHint =
      qBound(6, obj.value("fontSizeHint").toInt(d.fontSizeHint), 48);
  l.fontSizeBadge =
      qBound(6, obj.value("fontSizeBadge").toInt(d.fontSizeBadge), 48);
  l.pageHeaderHeight =
      qBound(36, obj.value("pageHeaderHeight").toInt(d.pageHeaderHeight), 64);
  l.paddingSmall =
      qBound(0, obj.value("paddingSmall").toInt(d.paddingSmall), 32);
  l.paddingNormal =
      qBound(0, obj.value("paddingNormal").toInt(d.paddingNormal), 32);
  l.paddingLarge =
      qBound(0, obj.value("paddingLarge").toInt(d.paddingLarge), 48);
  l.contentIndent =
      qBound(0, obj.value("contentIndent").toInt(d.contentIndent), 64);
  l.fontFamily = obj.value("fontFamily").toString(d.fontFamily);
  l.fontFamilyMono = obj.value("fontFamilyMono").toString(d.fontFamilyMono);
  return l;
}

// ============================================================================
// ThemeData::Animations
// ============================================================================

QJsonObject ThemeData::Animations::toJson() const {
  return {{"enabled", enabled},
          {"buttonGlow", buttonGlow},
          {"glowColor", glowColor},
          {"glowRadius", glowRadius},
          {"titleBarGradient", titleBarGradient}};
}

ThemeData::Animations ThemeData::Animations::fromJson(const QJsonObject &obj,
                                                      const Animations &d) {
  Animations a;
  a.enabled = obj.value("enabled").toBool(d.enabled);
  a.buttonGlow = obj.value("buttonGlow").toBool(d.buttonGlow);
  a.glowColor = validColor(obj, "glowColor", d.glowColor);
  a.glowRadius = obj.value("glowRadius").toInt(d.glowRadius);
  a.titleBarGradient = obj.value("titleBarGradient").toBool(d.titleBarGradient);
  return a;
}

// ============================================================================
// ThemeData — top-level serialization
// ============================================================================

QJsonObject ThemeData::toJson() const {
  QJsonObject obj;
  obj["type"] = "aio_theme";
  obj["version"] = schemaVersion;
  obj["name"] = name;
  obj["colors"] = colors.toJson();
  obj["toggle"] = toggle.toJson();
  obj["buttons"] = buttons.toJson();
  obj["widgets"] = widgets.toJson();
  obj["overlay"] = overlay.toJson();
  obj["layout"] = layout.toJson();
  obj["animations"] = animations.toJson();
  return obj;
}

ThemeData ThemeData::fromJson(const QJsonObject &obj) {
  ThemeData defaults; // Classic Gold defaults

  // Version check for future migrations
  int version = obj.value("version").toInt(1);
  Q_UNUSED(version); // Will be used when schema evolves

  ThemeData t;
  t.schemaVersion = version;
  t.name = obj.value("name").toString(defaults.name);
  t.colors = Colors::fromJson(obj.value("colors").toObject(), defaults.colors);
  t.toggle =
      ToggleTokens::fromJson(obj.value("toggle").toObject(), defaults.toggle);
  t.buttons =
      Buttons::fromJson(obj.value("buttons").toObject(), defaults.buttons);
  t.widgets =
      Widgets::fromJson(obj.value("widgets").toObject(), defaults.widgets);
  t.overlay = OverlayTokens::fromJson(obj.value("overlay").toObject(),
                                      defaults.overlay);
  t.layout = Layout::fromJson(obj.value("layout").toObject(), defaults.layout);
  t.animations = Animations::fromJson(obj.value("animations").toObject(),
                                      defaults.animations);
  return t;
}

// ============================================================================
// Built-in Presets
// ============================================================================

ThemeData ThemeData::classicGold() {
  // Default constructor already sets Classic Gold values
  return ThemeData();
}

ThemeData ThemeData::light() {
  ThemeData t;
  t.name = "Light";

  // Window & backgrounds
  t.colors.windowBg = "#F5F5F5";
  t.colors.windowSurface = "#FFFFFF";
  t.colors.titleBarBg = "#E8E8E8";
  t.colors.containerBg = "#F0F0F0";
  t.colors.overlayBg = "#E0E0E0";

  // Text
  t.colors.textPrimary = "#1A1A1A";
  t.colors.textSecondary = "#666666";
  t.colors.textHint = "#888888";
  t.colors.textAccent = "#8B6914";
  t.colors.textAccentSubtle = "#8B691419";

  // Status
  t.colors.success = "#27AE60";
  t.colors.warning = "#E67E22";
  t.colors.error = "#C0392B";

  // Icons
  t.colors.iconColor = "#8B6914";

  // Toggle
  t.toggle.onBg = "#34C759";
  t.toggle.offBg = "#CCCCCC";
  t.toggle.handleColor = "#FFFFFF";

  // Buttons — primary
  t.buttons.primary.bg = "#8B6914";
  t.buttons.primary.text = "#FFFFFF";
  t.buttons.primary.border = "#8B6914";
  t.buttons.primary.hoverBg = "#A07A1A";
  t.buttons.primary.hoverBorder = "#A07A1A";
  t.buttons.primary.hoverText = "#FFFFFF";
  t.buttons.primary.pressedBg = "#765A10";

  // Buttons — confirm
  t.buttons.confirm.bg = "#E0E0E0";
  t.buttons.confirm.text = "#1A1A1A";
  t.buttons.confirm.border = "#CCCCCC";
  t.buttons.confirm.hoverBg = "#4CAF50";
  t.buttons.confirm.hoverBorder = "#45A049";
  t.buttons.confirm.hoverText = "#FFFFFF";
  t.buttons.confirm.pressedBg = "#388E3C";

  // Buttons — cancel
  t.buttons.cancel.bg = "#E0E0E0";
  t.buttons.cancel.text = "#1A1A1A";
  t.buttons.cancel.border = "#CCCCCC";
  t.buttons.cancel.hoverBg = "#E53935";
  t.buttons.cancel.hoverBorder = "#D32F2F";
  t.buttons.cancel.hoverText = "#FFFFFF";
  t.buttons.cancel.pressedBg = "#C62828";

  // Buttons — neutral
  t.buttons.neutral.bg = "#E0E0E0";
  t.buttons.neutral.text = "#1A1A1A";
  t.buttons.neutral.border = "#CCCCCC";
  t.buttons.neutral.hoverBg = "#D0C0A0";
  t.buttons.neutral.hoverBorder = "#8B6914";
  t.buttons.neutral.hoverText = "";
  t.buttons.neutral.pressedBg = "#C0B090";

  // Buttons — action
  t.buttons.action.bg = "#4CAF50";
  t.buttons.action.text = "#FFFFFF";
  t.buttons.action.border = "#45A049";
  t.buttons.action.hoverBg = "#45A049";
  t.buttons.action.pressedBg = "#388E3C";

  // Buttons — apply dirty
  t.buttons.applyDirty.bg = "#D0C0A0";
  t.buttons.applyDirty.text = "#8B6914";
  t.buttons.applyDirty.border = "#8B6914";
  t.buttons.applyDirty.hoverBg = "#E0D0B0";
  t.buttons.applyDirty.hoverBorder = "#A07A1A";
  t.buttons.applyDirty.hoverText = "#A07A1A";
  t.buttons.applyDirty.pressedBg = "#C0B090";

  // Buttons — apply clean
  t.buttons.applyClean.bg = "#F0F0F0";
  t.buttons.applyClean.text = "#AAAAAA";
  t.buttons.applyClean.border = "#DDDDDD";
  t.buttons.applyClean.hoverBg = "#E8E8E8";
  t.buttons.applyClean.hoverBorder = "#CCCCCC";
  t.buttons.applyClean.hoverText = "#999999";
  t.buttons.applyClean.pressedBg = "#E0E0E0";

  // Nav buttons
  t.buttons.navActive.bg = "#DDDDDD";
  t.buttons.navActive.text = "#8B6914";
  t.buttons.navActive.border = "transparent";
  t.buttons.navInactive.bg = "transparent";
  t.buttons.navInactive.text = "#666666";
  t.buttons.navInactive.border = "transparent";
  t.buttons.navInactive.hoverBg = "#EEEEEE";
  t.buttons.navInactive.hoverText = "#8B6914";

  // Close button
  t.buttons.close.bg = "#E0E0E0";
  t.buttons.close.border = "#CCCCCC";
  t.buttons.close.hoverBg = "#E53935";
  t.buttons.close.hoverBorder = "#E53935";
  t.buttons.close.pressedBg = "#C62828";

  // Title minimize button
  t.buttons.titleMinimize.bg = "#E0E0E0";
  t.buttons.titleMinimize.border = "#CCCCCC";
  t.buttons.titleMinimize.hoverBg = "#D0C0A0";
  t.buttons.titleMinimize.hoverBorder = "#8B6914";
  t.buttons.titleMinimize.pressedBg = "#C0B090";

  // Title tray button
  t.buttons.titleTray.bg = "#E0E0E0";
  t.buttons.titleTray.border = "#CCCCCC";
  t.buttons.titleTray.hoverBg = "#5DADE2";
  t.buttons.titleTray.hoverBorder = "#5DADE2";
  t.buttons.titleTray.pressedBg = "#3498DB";

  // Widgets
  t.widgets.groupboxBorder = "#CCCCCC";
  t.widgets.groupboxTitle = "#8B6914";
  t.widgets.inputBg = "#FFFFFF";
  t.widgets.inputBorder = "#CCCCCC";
  t.widgets.inputFocusBorder = "#8B6914";
  t.widgets.comboboxBg = "#FFFFFF";
  t.widgets.comboboxBorder = "#CCCCCC";
  t.widgets.comboboxDropdownBg = "#F0F0F0";
  t.widgets.comboboxDropdownHover = "#E0E0E0";
  t.widgets.comboboxSelectionBg = "#E0E0E0";
  t.widgets.popupBg = "#FFFFFF";
  t.widgets.popupBorder = "#8B6914";
  t.widgets.scrollbarHandle = "#CCCCCC";
  t.widgets.scrollbarTrack = "#F0F0F0";
  t.widgets.listBg = "#FFFFFF";
  t.widgets.listBorder = "#CCCCCC";
  t.widgets.listSelectedBg = "#8B6914";
  t.widgets.listSelectedText = "#FFFFFF";
  t.widgets.tabBg = "#E0E0E0";
  t.widgets.tabBorder = "#CCCCCC";
  t.widgets.tabSelectedBg = "#FFFFFF";
  t.widgets.tabSelectedBorder = "#8B6914";
  t.widgets.tabText = "#666666";
  t.widgets.tabSelectedText = "#333333";
  t.widgets.sliderGroove = "#CCCCCC";
  t.widgets.sliderHandle = "#8B6914";
  t.widgets.badgeBg = "#27AE60";
  t.widgets.badgeText = "#FFFFFF";
  t.widgets.infoBannerBg = "#F0F0F0";
  t.widgets.infoBannerBorder = "#CCCCCC";
  t.widgets.infoBannerText = "#555555";
  t.widgets.packCardBg = "#FFFFFF";
  t.widgets.packCardBorder = "#DDDDDD";
  t.widgets.packCardHoverBorder = "#8B6914";
  t.widgets.userCardBg = "#F0F0F0";
  t.widgets.userCardBorder = "#CCCCCC";
  t.widgets.packSectionLabel = "#8B6914";
  t.widgets.packSplitter = "#DDDDDD";
  t.widgets.packProgressBg = "#E0E0E0";
  t.widgets.packProgressFill = "#8B6914";
  t.widgets.pageHeaderBg = "#F0F0F0";
  t.widgets.pageHeaderBorder = "#DDDDDD";
  t.widgets.pageHeaderText = "#8B6914";

  // Overlay
  t.overlay.panelBg = "#F5F5F5CC";
  t.overlay.panelBorder = "#8B6914";
  t.overlay.headerBg = "#E8E8E8";
  t.overlay.itemHoverBg = "#E0E0E0";
  t.overlay.textPrimary = "#1A1A1A";
  t.overlay.textSecondary = "#666666";
  t.overlay.toggleOn = "#34C759";
  t.overlay.toggleOff = "#CCCCCC";
  t.overlay.iconBg = "#F5F5F5CC";
  t.overlay.iconHoverBg = "#E0E0E0CC";
  t.overlay.expanderColor = "#666666";

  return t;
}

ThemeData ThemeData::navyBlue() {
  ThemeData t;
  t.name = "Navy Blue";

  // Window & backgrounds
  t.colors.windowBg = "#0D1B2A";
  t.colors.windowSurface = "#1B2838";
  t.colors.titleBarBg = "#0D1B2A";
  t.colors.containerBg = "#101E2E"; // Darkened from #152535
  t.colors.overlayBg = "#0D1B2A";

  // Text
  t.colors.textPrimary = "#E0E0E0";
  t.colors.textSecondary = "#8899AA";
  t.colors.textHint = "#667788";
  t.colors.textAccent = "#5DADE2";
  t.colors.textAccentSubtle = "#5DADE219";

  // Status
  t.colors.success = "#2ECC71";
  t.colors.warning = "#F39C12";
  t.colors.error = "#E74C3C";

  // Icons
  t.colors.iconColor = "#5DADE2";

  // Toggle
  t.toggle.onBg = "#3498DB";
  t.toggle.offBg = "#2A3A4A";
  t.toggle.handleColor = "#FFFFFF";

  // Buttons — primary (blue accent)
  t.buttons.primary.bg = "#1A3050";
  t.buttons.primary.text = "#5DADE2";
  t.buttons.primary.border = "#5DADE2";
  t.buttons.primary.hoverBg = "#2A4060";
  t.buttons.primary.hoverBorder = "#7EC8E3";
  t.buttons.primary.hoverText = "#7EC8E3";
  t.buttons.primary.pressedBg = "#152840";

  // Buttons — confirm
  t.buttons.confirm.bg = "#1B2838";
  t.buttons.confirm.text = "#E0E0E0";
  t.buttons.confirm.border = "#334455";
  t.buttons.confirm.hoverBg = "#2E7D32";
  t.buttons.confirm.hoverBorder = "#4CAF50";
  t.buttons.confirm.hoverText = "";
  t.buttons.confirm.pressedBg = "#1B5E20";

  // Buttons — cancel
  t.buttons.cancel.bg = "#1B2838";
  t.buttons.cancel.text = "#E0E0E0";
  t.buttons.cancel.border = "#334455";
  t.buttons.cancel.hoverBg = "#8B0000";
  t.buttons.cancel.hoverBorder = "#A00000";
  t.buttons.cancel.hoverText = "#FFFFFF";
  t.buttons.cancel.pressedBg = "#6B0000";

  // Buttons — neutral
  t.buttons.neutral.bg = "#1B2838";
  t.buttons.neutral.text = "#E0E0E0";
  t.buttons.neutral.border = "#334455";
  t.buttons.neutral.hoverBg = "#253848";
  t.buttons.neutral.hoverBorder = "#5DADE2";
  t.buttons.neutral.hoverText = "";
  t.buttons.neutral.pressedBg = "#1A3040";

  // Buttons — action
  t.buttons.action.bg = "#2E7D32";
  t.buttons.action.text = "#E0E0E0";
  t.buttons.action.border = "#4CAF50";
  t.buttons.action.hoverBg = "#4CAF50";
  t.buttons.action.pressedBg = "#1B5E20";

  // Buttons — apply dirty
  t.buttons.applyDirty.bg = "#253848";
  t.buttons.applyDirty.text = "#5DADE2";
  t.buttons.applyDirty.border = "#5DADE2";
  t.buttons.applyDirty.hoverBg = "#2A4060";
  t.buttons.applyDirty.hoverBorder = "#7EC8E3";
  t.buttons.applyDirty.hoverText = "#7EC8E3";
  t.buttons.applyDirty.pressedBg = "#1A3040";

  // Buttons — apply clean
  t.buttons.applyClean.bg = "#152030";
  t.buttons.applyClean.text = "#556677";
  t.buttons.applyClean.border = "#1B2838";
  t.buttons.applyClean.hoverBg = "#1B2838";
  t.buttons.applyClean.hoverBorder = "#334455";
  t.buttons.applyClean.hoverText = "#778899";
  t.buttons.applyClean.pressedBg = "#0D1B2A";

  // Nav buttons
  t.buttons.navActive.bg = "#1B2838";
  t.buttons.navActive.text = "#5DADE2";
  t.buttons.navActive.border = "transparent";
  t.buttons.navInactive.bg = "transparent";
  t.buttons.navInactive.text = "#8899AA";
  t.buttons.navInactive.border = "transparent";
  t.buttons.navInactive.hoverBg = "#152030";
  t.buttons.navInactive.hoverText = "#5DADE2";

  // Close button
  t.buttons.close.bg = "#1B2838";
  t.buttons.close.border = "#334455";
  t.buttons.close.hoverBg = "#8B0000";
  t.buttons.close.hoverBorder = "#8B0000";
  t.buttons.close.pressedBg = "#6B0000";

  // Title minimize button
  t.buttons.titleMinimize.bg = "#1B2838";
  t.buttons.titleMinimize.border = "#334455";
  t.buttons.titleMinimize.hoverBg = "#253848";
  t.buttons.titleMinimize.hoverBorder = "#5DADE2";
  t.buttons.titleMinimize.pressedBg = "#1A3040";

  // Title tray button
  t.buttons.titleTray.bg = "#1B2838";
  t.buttons.titleTray.border = "#334455";
  t.buttons.titleTray.hoverBg = "#4A90D9";
  t.buttons.titleTray.hoverBorder = "#4A90D9";
  t.buttons.titleTray.pressedBg = "#3A73B0";

  // Widgets
  t.widgets.groupboxBorder = "#334455";
  t.widgets.groupboxTitle = "#5DADE2";
  t.widgets.inputBg = "#0D1B2A";
  t.widgets.inputBorder = "#334455";
  t.widgets.inputFocusBorder = "#5DADE2";
  t.widgets.comboboxBg = "#1B2838";
  t.widgets.comboboxBorder = "#334455";
  t.widgets.comboboxDropdownBg = "#1B2838";
  t.widgets.comboboxDropdownHover = "#253848";
  t.widgets.comboboxSelectionBg = "#253848";
  t.widgets.popupBg = "#1B2838";
  t.widgets.popupBorder = "#5DADE2";
  t.widgets.scrollbarHandle = "#334455";
  t.widgets.scrollbarTrack = "#0D1B2A";
  t.widgets.listBg = "#0D1B2A";
  t.widgets.listBorder = "#334455";
  t.widgets.listSelectedBg = "#5DADE2";
  t.widgets.listSelectedText = "#0D1B2A";
  t.widgets.tabBg = "#1B2838";
  t.widgets.tabBorder = "#334455";
  t.widgets.tabSelectedBg = "#253848";
  t.widgets.tabSelectedBorder = "#5DADE2";
  t.widgets.tabText = "#7B8FA0";
  t.widgets.tabSelectedText = "#D4E6F1";
  t.widgets.sliderGroove = "#1B2838";
  t.widgets.sliderHandle = "#5DADE2";
  t.widgets.badgeBg = "#2ECC71";
  t.widgets.badgeText = "#FFFFFF";
  t.widgets.infoBannerBg = "#152030";
  t.widgets.infoBannerBorder = "#334455";
  t.widgets.infoBannerText = "#8899AA";
  t.widgets.packCardBg = "#1B2838";
  t.widgets.packCardBorder = "#334455";
  t.widgets.packCardHoverBorder = "#5DADE2";
  t.widgets.userCardBg = "#15202D";
  t.widgets.userCardBorder = "#293A4D";
  t.widgets.packSectionLabel = "#5DADE2";
  t.widgets.packSplitter = "#334455";
  t.widgets.packProgressBg = "#0D1B2A";
  t.widgets.packProgressFill = "#5DADE2";
  t.widgets.pageHeaderBg = "#0D1B2A";
  t.widgets.pageHeaderBorder = "#253848";
  t.widgets.pageHeaderText = "#5DADE2";

  // Overlay
  t.overlay.panelBg = "#0D1B2ACC";
  t.overlay.panelBorder = "#5DADE2";
  t.overlay.headerBg = "#1B2838";
  t.overlay.itemHoverBg = "#253848";
  t.overlay.textPrimary = "#E0E0E0";
  t.overlay.textSecondary = "#8899AA";
  t.overlay.toggleOn = "#3498DB";
  t.overlay.toggleOff = "#2A3A4A";
  t.overlay.iconBg = "#0D1B2ACC";
  t.overlay.iconHoverBg = "#1B2838CC";
  t.overlay.expanderColor = "#667788";

  return t;
}

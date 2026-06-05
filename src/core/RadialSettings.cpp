/**
 * @file RadialSettings.cpp
 * @brief Serialization and defaults for RadialSettings
 *
 * DO NOT ADD:
 * - UI code (belongs in RadialTabWidget)
 * - File I/O (belongs in RadialSettingsManager)
 */

#include "RadialSettings.h"

#include <QJsonArray>

// ============================================================================
// RadialElementConfig
// ============================================================================

QJsonObject RadialElementConfig::toJson() const {
  QJsonObject obj;
  obj["enabled"] = enabled;
  obj["sortOrder"] = sortOrder;
  obj["scanCode"] = scanCode;
  obj["modifiers"] = modifiers;
  return obj;
}

RadialElementConfig RadialElementConfig::fromJson(const QJsonObject &obj) {
  RadialElementConfig cfg;
  cfg.enabled = obj["enabled"].toBool(true);
  cfg.sortOrder = obj["sortOrder"].toInt(0);
  cfg.scanCode = obj["scanCode"].toInt(0);
  cfg.modifiers = obj["modifiers"].toInt(0);
  return cfg;
}

// ============================================================================
// Helper: serialize/deserialize QMap<QString, RadialElementConfig>
// ============================================================================

static QJsonObject elementMapToJson(
    const QMap<QString, RadialElementConfig> &map) {
  QJsonObject obj;
  for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
    obj[it.key()] = it.value().toJson();
  }
  return obj;
}

static QMap<QString, RadialElementConfig> elementMapFromJson(
    const QJsonObject &obj) {
  QMap<QString, RadialElementConfig> map;
  for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
    map[it.key()] = RadialElementConfig::fromJson(it.value().toObject());
  }
  return map;
}

// ============================================================================
// RadialSettings
// ============================================================================

QJsonObject RadialSettings::toJson() const {
  QJsonObject obj;

  // File identification (versioned file format rule)
  obj["type"] = QStringLiteral("radial_settings");
  obj["version"] = schemaVersion;

  // Global
  obj["radialEnabled"] = radialEnabled;
  obj["iconStyle"] = iconStyle;

  // Mount wheel
  QJsonObject mountObj;
  mountObj["enabled"] = mountWheelEnabled;
  mountObj["hotkey"] = mountHotkey;
  mountObj["hotkeyModifiers"] = mountHotkeyModifiers;
  mountObj["elements"] = elementMapToJson(mounts);
  obj["mountWheel"] = mountObj;

  // Novelty wheel
  QJsonObject noveltyObj;
  noveltyObj["enabled"] = noveltyWheelEnabled;
  noveltyObj["hotkey"] = noveltyHotkey;
  noveltyObj["hotkeyModifiers"] = noveltyHotkeyModifiers;
  noveltyObj["elements"] = elementMapToJson(novelties);
  obj["noveltyWheel"] = noveltyObj;

  // Marker wheel
  QJsonObject markerObj;
  markerObj["enabled"] = markerWheelEnabled;
  markerObj["hotkey"] = markerHotkey;
  markerObj["hotkeyModifiers"] = markerHotkeyModifiers;
  markerObj["elements"] = elementMapToJson(markers);
  obj["markerWheel"] = markerObj;

  // Display
  QJsonObject displayObj;
  displayObj["wheelScale"] = static_cast<double>(wheelScale);
  displayObj["centerScale"] = static_cast<double>(centerScale);
  displayObj["opacity"] = static_cast<double>(opacity);
  displayObj["animationTimeMs"] = animationTimeMs;
  displayObj["displayDelayMs"] = displayDelayMs;
  obj["display"] = displayObj;

  // Interaction
  QJsonObject interObj;
  interObj["centerBehavior"] = static_cast<int>(centerBehavior);
  interObj["noHoldMode"] = noHoldMode;
  interObj["clickSelectMode"] = clickSelectMode;
  interObj["resetCursorAfterKeybind"] = resetCursorAfterKeybind;
  interObj["lockCameraWhenOverlayed"] = lockCameraWhenOverlayed;
  obj["interaction"] = interObj;

  // Queuing
  QJsonObject queueObj;
  queueObj["enableQueuing"] = enableQueuing;
  queueObj["maxQueueWaitMs"] = maxQueueWaitMs;
  queueObj["conditionalDelayMs"] = conditionalDelayMs;
  obj["queuing"] = queueObj;

  // Smart features
  obj["smartRadialMounts"] = smartRadialMounts;

  return obj;
}

RadialSettings RadialSettings::fromJson(const QJsonObject &obj) {
  RadialSettings s;

  s.schemaVersion = obj["version"].toInt(1);

  // Global
  s.radialEnabled = obj["radialEnabled"].toBool(true);
  s.iconStyle = obj["iconStyle"].toString("svg");

  // Mount wheel
  QJsonObject mountObj = obj["mountWheel"].toObject();
  s.mountWheelEnabled = mountObj["enabled"].toBool(true);
  s.mountHotkey = mountObj["hotkey"].toInt(0x58);             // VK_X
  s.mountHotkeyModifiers = mountObj["hotkeyModifiers"].toInt(1); // GW2: 1=Alt
  s.mounts = elementMapFromJson(mountObj["elements"].toObject());

  // Novelty wheel
  QJsonObject noveltyObj = obj["noveltyWheel"].toObject();
  s.noveltyWheelEnabled = noveltyObj["enabled"].toBool(true);
  s.noveltyHotkey = noveltyObj["hotkey"].toInt(0x4E);          // VK_N
  s.noveltyHotkeyModifiers = noveltyObj["hotkeyModifiers"].toInt(1); // GW2: 1=Alt
  s.novelties = elementMapFromJson(noveltyObj["elements"].toObject());

  // Marker wheel
  QJsonObject markerObj = obj["markerWheel"].toObject();
  s.markerWheelEnabled = markerObj["enabled"].toBool(true);
  s.markerHotkey = markerObj["hotkey"].toInt(0x4D);            // VK_M
  s.markerHotkeyModifiers = markerObj["hotkeyModifiers"].toInt(1); // GW2: 1=Alt
  s.markers = elementMapFromJson(markerObj["elements"].toObject());

  // Migrate old Qt modifier flags → GW2 bitmask (1=Alt, 2=Ctrl, 4=Shift).
  // GW2 bitmask max is 7 (Ctrl+Shift+Alt), so values > 7 are Qt flags
  // from pre-migration saves (e.g., Qt::ControlModifier = 0x04000000).
  auto migrateModifiers = [](int &mods) {
    if (mods > 7) {
      int gw2Mods = 0;
      if (mods & 0x08000000) gw2Mods |= 1; // Qt::AltModifier → Alt
      if (mods & 0x04000000) gw2Mods |= 2; // Qt::ControlModifier → Ctrl
      if (mods & 0x02000000) gw2Mods |= 4; // Qt::ShiftModifier → Shift
      mods = gw2Mods;
    }
  };
  migrateModifiers(s.mountHotkeyModifiers);
  migrateModifiers(s.noveltyHotkeyModifiers);
  migrateModifiers(s.markerHotkeyModifiers);

  // Display
  QJsonObject displayObj = obj["display"].toObject();
  s.wheelScale = static_cast<float>(displayObj["wheelScale"].toDouble(1.0));
  s.centerScale = static_cast<float>(displayObj["centerScale"].toDouble(0.35));
  s.opacity = static_cast<float>(displayObj["opacity"].toDouble(1.0));
  s.animationTimeMs = displayObj["animationTimeMs"].toInt(150);
  s.displayDelayMs = displayObj["displayDelayMs"].toInt(0);

  // Interaction
  QJsonObject interObj = obj["interaction"].toObject();
  s.centerBehavior = static_cast<RadialCenterBehavior>(
      interObj["centerBehavior"].toInt(0));
  s.noHoldMode = interObj["noHoldMode"].toBool(false);
  s.clickSelectMode = interObj["clickSelectMode"].toBool(false);
  s.resetCursorAfterKeybind =
      interObj["resetCursorAfterKeybind"].toBool(true);
  s.lockCameraWhenOverlayed =
      interObj["lockCameraWhenOverlayed"].toBool(true);

  // Queuing
  QJsonObject queueObj = obj["queuing"].toObject();
  s.enableQueuing = queueObj["enableQueuing"].toBool(false);
  s.maxQueueWaitMs = queueObj["maxQueueWaitMs"].toInt(5000);
  s.conditionalDelayMs = queueObj["conditionalDelayMs"].toInt(500);

  // Smart features
  s.smartRadialMounts = obj["smartRadialMounts"].toBool(false);

  return s;
}

// ============================================================================
// defaults() — populates all elements with GW2 standard layout
// ============================================================================

static RadialElementConfig makeElement(int order, int scanCode = 0,
                                       int modifiers = 0) {
  RadialElementConfig cfg;
  cfg.enabled = true;
  cfg.sortOrder = order;
  cfg.scanCode = scanCode;
  cfg.modifiers = modifiers;
  return cfg;
}

RadialSettings RadialSettings::defaults() {
  RadialSettings s;

  // --- Mounts (10) ---
  // Default scancodes are 0 — user must configure to match their in-game
  // bindings. Sort order matches GW2Radial's default layout.
  s.mounts["raptor"] = makeElement(0);
  s.mounts["springer"] = makeElement(1);
  s.mounts["skimmer"] = makeElement(2);
  s.mounts["jackal"] = makeElement(3);
  s.mounts["griffon"] = makeElement(4);
  s.mounts["beetle"] = makeElement(5);
  s.mounts["warclaw"] = makeElement(6);
  s.mounts["skyscale"] = makeElement(7);
  s.mounts["turtle"] = makeElement(8);
  s.mounts["skiff"] = makeElement(9);

  // --- Novelties (9) ---
  s.novelties["chair"] = makeElement(0);
  s.novelties["instrument"] = makeElement(1);
  s.novelties["heldItem"] = makeElement(2);
  s.novelties["travelToy"] = makeElement(3);
  s.novelties["tonic"] = makeElement(4);
  s.novelties["jadeWaypoint"] = makeElement(5);
  s.novelties["fishing"] = makeElement(6);
  s.novelties["scanForRift"] = makeElement(7);
  s.novelties["summonDoorway"] = makeElement(8);

  // --- Markers (10: 9 markers + clear) ---
  s.markers["arrow"] = makeElement(0);
  s.markers["circle"] = makeElement(1);
  s.markers["heart"] = makeElement(2);
  s.markers["square"] = makeElement(3);
  s.markers["star"] = makeElement(4);
  s.markers["spiral"] = makeElement(5);
  s.markers["triangle"] = makeElement(6);
  s.markers["x"] = makeElement(7);
  s.markers["clear"] = makeElement(8);

  return s;
}

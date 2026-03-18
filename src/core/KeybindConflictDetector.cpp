/**
 * @file KeybindConflictDetector.cpp
 * @brief Keybind conflict detector
 *
 * Warns if AIO hotkeys conflict with GW2 keybinds.
 *
 * DO NOT ADD:
 * - UI code
 * - Settings persistence
 */

#include "KeybindConflictDetector.h"

#include <QDebug>

KeybindConflictDetector::KeybindConflictDetector(QObject *parent)
    : QObject(parent) {
  // Map internal GW2 action IDs to friendly names
  m_actionNames = {
      {"MoveForward", "Move Forward"},
      {"MoveBackward", "Move Backward"},
      {"StrafeLeft", "Strafe Left"},
      {"StrafeRight", "Strafe Right"},
      {"TurnLeft", "Turn Left"},
      {"TurnRight", "Turn Right"},
      {"Dodge", "Dodge"},
      {"Jump", "Jump"},
      {"SwapWeapons", "Swap Weapons"},
      {"WeaponSkill1", "Skill 1"},
      {"WeaponSkill2", "Skill 2"},
      {"WeaponSkill3", "Skill 3"},
      {"WeaponSkill4", "Skill 4"},
      {"WeaponSkill5", "Skill 5"},
      {"HealingSkill", "Healing Skill"},
      {"UtilitySkill1", "Utility 1"},
      {"UtilitySkill2", "Utility 2"},
      {"UtilitySkill3", "Utility 3"},
      {"EliteSkill", "Elite Skill"},
      {"ProfessionSkill1", "Profession 1"},
      {"ProfessionSkill2", "Profession 2"},
      {"ProfessionSkill3", "Profession 3"},
      {"ProfessionSkill4", "Profession 4"},
      {"ProfessionSkill5", "Profession 5"},
      {"SpecialAction", "Special Action"},
      {"Interact", "Interact"},
      {"ShowOptions", "Options"},
      {"ToggleFullScreen", "Toggle Fullscreen"},
      {"StowDrawWeapon", "Stow/Draw Weapon"},
      {"AboutFace", "About Face"},
      {"LookBehind", "Look Behind"},
      {"ToggleAutorun", "Toggle Autorun"},
      {"ShowMounts", "Mounts"},
      {"MountAbility1", "Mount Ability 1"},
      {"MountAbility2", "Mount Ability 2"},
      {"ToggleMap", "Toggle Map"},
      {"ToggleMinimap", "Toggle Minimap"},
      {"TargetNearest", "Target Nearest"},
      {"TargetNext", "Target Next"},
      {"TargetPrevious", "Target Previous"},
      {"SquadMarker1", "Squad Marker 1"},
      {"SquadMarker2", "Squad Marker 2"},
      {"SquadMarker3", "Squad Marker 3"},
  };
}

bool KeybindConflictDetector::loadGW2Keybinds(const QString &gw2Path) {
  // GW2 stores keybinds in AppData
  QString appData =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  QString gw2AppData = QDir(appData).filePath("Guild Wars 2");
  QString inputPath = QDir(gw2AppData).filePath("InputBinds.xml");

  // Alternative: check in game directory
  if (!QFile::exists(inputPath)) {
    inputPath = QDir(gw2Path).filePath("InputBinds.xml");
  }

  QFile file(inputPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Could not open GW2 keybinds file:" << inputPath;
    return false;
  }

  QString content = QString::fromUtf8(file.readAll());
  parseInputBindings(content);

  qInfo() << "Loaded" << m_gw2Keybinds.size() << "GW2 keybinds";
  return true;
}

void KeybindConflictDetector::parseInputBindings(const QString &content) {
  m_gw2Keybinds.clear();

  // Simple string-based parsing for InputBinds.xml
  int pos = 0;
  while ((pos = content.indexOf(QLatin1String("<Binding"), pos)) != -1) {
    int end = content.indexOf(QLatin1String("/>"), pos);
    if (end == -1)
      break;

    QString binding = content.mid(pos, end - pos);

    if (binding.contains(QLatin1String("Device=\"Keyboard\""))) {
      int keyStart = binding.indexOf(QLatin1String("Key=\""));
      if (keyStart != -1) {
        keyStart += 5;
        int keyEnd = binding.indexOf(QLatin1Char('"'), keyStart);
        QString key = binding.mid(keyStart, keyEnd - keyStart);

        int actionStart = binding.indexOf(QLatin1String("Action=\""));
        if (actionStart != -1) {
          actionStart += 8;
          int actionEnd = binding.indexOf(QLatin1Char('"'), actionStart);
          QString action = binding.mid(actionStart, actionEnd - actionStart);

          m_gw2Keybinds[normalizeKeybind(key)] = action;
        }
      }
    }
    pos = end + 2;
  }
}

QString
KeybindConflictDetector::normalizeKeybind(const QString &keybind) const {
  QString normalized = keybind.toUpper();

  // Normalize common variations
  normalized.replace("CTRL", "Ctrl");
  normalized.replace("ALT", "Alt");
  normalized.replace("SHIFT", "Shift");
  normalized.replace("SPACE", "Space");
  normalized.replace("RETURN", "Return");
  normalized.replace("ENTER", "Return");

  return normalized;
}

bool KeybindConflictDetector::hasConflict(const QString &keybind) const {
  return m_gw2Keybinds.contains(normalizeKeybind(keybind));
}

QString KeybindConflictDetector::getGW2Action(const QString &keybind) const {
  QString normalized = normalizeKeybind(keybind);
  QString actionId = m_gw2Keybinds.value(normalized);
  return m_actionNames.value(actionId, actionId);
}

QList<KeybindConflictDetector::ConflictInfo>
KeybindConflictDetector::checkConflicts(
    const QMap<QString, QString> &aioBinds) const {
  QList<ConflictInfo> conflicts;

  for (auto it = aioBinds.begin(); it != aioBinds.end(); ++it) {
    QString normalized = normalizeKeybind(it.value());

    if (m_gw2Keybinds.contains(normalized)) {
      ConflictInfo info;
      info.keybind = it.value();
      info.aioAction = it.key();
      info.gw2Action = getGW2Action(it.value());
      conflicts.append(info);
    }
  }

  if (!conflicts.isEmpty()) {
    emit const_cast<KeybindConflictDetector *>(this)->conflictsDetected(
        conflicts);
  }

  return conflicts;
}

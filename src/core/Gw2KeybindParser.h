/**
 * @file Gw2KeybindParser.h
 * @brief Parses GW2 InputBinds XML to extract mount/novelty keybinds.
 *
 * GW2 exports keybinds as XML via F11 → Control Options → Export.
 * The file lands in: Documents/Guild Wars 2/InputBinds/<user-chosen-name>.xml
 *
 * Format:
 *   <action name="Raptor Mount/Dismount" id="155" device="Keyboard" button="96"/>
 *   <action name="Skimmer Mount/Dismount" id="157" device="Keyboard" button="98" mod="2"/>
 *
 * - button = Windows Virtual Key Code (e.g., 97 = VK_NUMPAD1, 66 = VK_B)
 * - mod = bitmask: 1=Alt, 2=Ctrl, 4=Shift (per GW2 docs, binary 001/010/100)
 * - device="None" means unbound
 *
 * Header-only utility — no .cpp needed.
 */

#pragma once

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QStandardPaths>
#include <QString>
#include <QXmlStreamReader>

struct Gw2Keybind {
  int virtualKey = 0; // Windows VK code (0 = unbound)
  int modifiers = 0;  // Bitmask: 1=Shift, 2=Ctrl, 4=Alt
};

/**
 * @brief Convert a GW2 internal button ID to a Windows Virtual-Key (VK) code.
 *
 * GW2 uses its own internal key numbering system that differs from standard
 * Windows VK codes. Empirically verified through 5 rounds of XML import/export
 * testing (June 2026) on a Latin American keyboard.
 *
 * Range-based conversions:
 *   Letters A-Z (65-90):   VK = GW2 (identical)
 *   Digits 0-9 (48-57):   VK = GW2 (identical)
 *   Numpad 0-9 (95-104):  VK = GW2 + 1
 *   F1-F12 (32-43):       VK = GW2 + 80
 *   F13-F24 (112-123):    VK = GW2 + 12
 *
 * All other keys use a direct lookup table (no formula).
 *
 * @param gw2Button The button value from the GW2 XML export
 * @return The corresponding Windows VK code, or 0 if unmapped
 */
static int gw2ButtonToVk(int gw2Button) {
  // --- Range-based conversions (continuous ranges) ---

  // Letters A-Z: GW2 65-90 = VK 65-90 (identical)
  if (gw2Button >= 65 && gw2Button <= 90) return gw2Button;

  // Digits 0-9 (top row): GW2 48-57 = VK 48-57 (identical)
  if (gw2Button >= 48 && gw2Button <= 57) return gw2Button;

  // Numpad digits 0-9: GW2 95-104 → VK 96-105 (add 1)
  if (gw2Button >= 95 && gw2Button <= 104) return gw2Button + 1;

  // F1-F12: GW2 32-43 → VK 112-123 (add 80)
  if (gw2Button >= 32 && gw2Button <= 43) return gw2Button + 80;

  // F13-F24: GW2 112-123 → VK 124-135 (add 12)
  if (gw2Button >= 112 && gw2Button <= 123) return gw2Button + 12;

  // --- Individual key lookups (no formula, verified one by one) ---
  switch (gw2Button) {
  // Modifier keys
  case 0:
    return 0xA4; // Left Alt (VK_LMENU = 164)
  case 1:
    return 0xA2; // Left Ctrl (VK_LCONTROL = 162)
  case 2:
    return 0xA0; // Left Shift (VK_LSHIFT = 160)
  case 109:
    return 0xA5; // Right Alt (VK_RMENU = 165)
  case 110:
    return 0xA3; // Right Ctrl (VK_RCONTROL = 163)

  // OEM / Punctuation keys (VK_OEM codes = physical key positions,
  // layout-independent. Characters shown vary by keyboard layout.)
  case 3:
    return 0xDE; // VK_OEM_7 (222) — LatAm: {  US: '
  case 4:
    return 0xDC; // VK_OEM_5 (220) — LatAm: }  US: backslash
  case 6:
    return 0xBC; // VK_OEM_COMMA (188) — comma key
  case 7:
    return 0xBD; // VK_OEM_MINUS (189) — LatAm: '  US: -
  case 8:
    return 0xBB; // VK_OEM_PLUS (187) — LatAm: ¿  US: =
  case 10:
    return 0xDB; // VK_OEM_4 (219) — LatAm: ´  US: [
  case 12:
    return 0xBE; // VK_OEM_PERIOD (190) — period key
  case 13:
    return 0xDD; // VK_OEM_6 (221) — LatAm: +  US: ]
  case 14:
    return 0xBA; // VK_OEM_1 (186) — LatAm: ñ  US: ;
  case 15:
    return 0xBF; // VK_OEM_2 (191) — LatAm: -  US: /
  case 17:
    return 0xC0; // VK_OEM_3 (192) — LatAm: |  US: `
  case 111:
    return 0xE2; // VK_OEM_102 (226) — European/LatAm extra key: <

  // Control / Special keys
  case 5:
    return 0x14; // Caps Lock (VK_CAPITAL = 20)
  case 9:
    return 0x1B; // Escape (VK_ESCAPE = 27)
  case 11:
    return 0x90; // Num Lock (VK_NUMLOCK = 144)
  case 16:
    return 0x2C; // Print Screen (VK_SNAPSHOT = 44)
  case 18:
    return 0x08; // Backspace (VK_BACK = 8)
  case 19:
    return 0x2E; // Delete (VK_DELETE = 46)
  case 20:
    return 0x0D; // Enter (VK_RETURN = 13)
  case 21:
    return 0x20; // Space (VK_SPACE = 32)
  case 22:
    return 0x09; // Tab (VK_TAB = 9)
  case 23:
    return 0x23; // End (VK_END = 35) — inferred from position
  case 24:
    return 0x24; // Home (VK_HOME = 36)
  case 25:
    return 0x2D; // Insert (VK_INSERT = 45)

  // Navigation keys
  case 26:
    return 0x22; // Page Down (VK_NEXT = 34)
  case 27:
    return 0x21; // Page Up (VK_PRIOR = 33)
  case 28:
    return 0x28; // Down Arrow (VK_DOWN = 40)
  case 29:
    return 0x25; // Left Arrow (VK_LEFT = 37)
  case 30:
    return 0x27; // Right Arrow (VK_RIGHT = 39)
  case 31:
    return 0x26; // Up Arrow (VK_UP = 38)

  // Numpad operators (non-sequential offsets, individual lookup)
  case 91:
    return 0x6B; // Numpad + (VK_ADD = 107)
  case 92:
    return 0x6E; // Numpad . (VK_DECIMAL = 110)
  case 93:
    return 0x6F; // Numpad / (VK_DIVIDE = 111)
  case 94:
    return 0x6A; // Numpad * (VK_MULTIPLY = 106)
  case 105:
    return 0x0D; // Numpad Enter (VK_RETURN = 13, extended)
  case 106:
    return 0x6D; // Numpad - (VK_SUBTRACT = 109)

  // IME keys (East Asian keyboards, uncommon for game keybinds)
  case 107:
    return 0x1C; // IME Convert (VK_CONVERT = 28)
  case 108:
    return 0x1D; // IME Non-Convert (VK_NONCONVERT = 29)

  default:
    break;
  }

  // Unknown key: log warning and return 0 (unbound).
  // GW2 IDs 44-47, 58-64, 124+ (F25+) have no standard VK mapping.
  qWarning() << "Gw2KeybindParser: Unknown GW2 button ID" << gw2Button
             << "- no VK mapping available, treating as unbound";
  return 0;
}

class Gw2KeybindParser {
public:
  // GW2 action name → AIO settings key
  // Covers mounts, novelty-like actions, and skiff
  static inline const QMap<QString, QString> kActionToSettingsKey = {
      // Generic mount/dismount (used by Fast Mount Swap)
      {"Mount/Dismount", "_dismount"},
      // Mounts
      {"Raptor Mount/Dismount", "raptor"},
      {"Springer Mount/Dismount", "springer"},
      {"Skimmer Mount/Dismount", "skimmer"},
      {"Jackal Mount/Dismount", "jackal"},
      {"Griffon Mount/Dismount", "griffon"},
      {"Roller Beetle Mount/Dismount", "beetle"},
      {"Warclaw Mount/Dismount", "warclaw"},
      {"Skyscale Mount/Dismount", "skyscale"},
      {"Siege Turtle Mount/Dismount", "turtle"},
      {"Summon Skiff", "skiff"},
      // Novelty-like actions
      {"Start Fishing", "fishing"},
      {"Scan for Rift", "scanForRift"},
      {"Set Jade Bot Waypoint", "jadeWaypoint"},
  };

  /**
   * @brief Find the most recently modified XML in the InputBinds directory.
   * @return Absolute path to the newest XML, or empty string if none found.
   *
   * Searches: Documents/Guild Wars 2/InputBinds/
   */
  static QString findNewestExportFile() {
    QString docsPath =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString inputBindsDir =
        QDir(docsPath).filePath("Guild Wars 2/InputBinds");

    QDir dir(inputBindsDir);
    if (!dir.exists()) {
      qInfo() << "Gw2KeybindParser: InputBinds directory not found:"
              << inputBindsDir;
      return {};
    }

    QFileInfoList xmlFiles =
        dir.entryInfoList({"*.xml"}, QDir::Files, QDir::Time);
    if (xmlFiles.isEmpty()) {
      qInfo() << "Gw2KeybindParser: No XML files in" << inputBindsDir;
      return {};
    }

    // First entry is most recently modified
    qInfo() << "Gw2KeybindParser: Found" << xmlFiles.size()
            << "XML files, newest:" << xmlFiles.first().fileName();
    return xmlFiles.first().absoluteFilePath();
  }

  /**
   * @brief Parse a GW2 InputBinds XML file.
   * @param xmlPath Absolute path to the XML file.
   * @return Map of AIO settings key → keybind (only entries found in XML).
   *
   * Only parses keyboard bindings for known mount/novelty actions.
   * Entries with device="None" are skipped (unbound).
   */
  static QMap<QString, Gw2Keybind> parseFile(const QString &xmlPath) {
    QMap<QString, Gw2Keybind> result;

    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning() << "Gw2KeybindParser: Cannot open" << xmlPath;
      return result;
    }

    QXmlStreamReader xml(&file);
    int parsed = 0;

    while (!xml.atEnd() && !xml.hasError()) {
      xml.readNext();
      if (xml.isStartElement() && xml.name() == QLatin1String("action")) {
        auto attrs = xml.attributes();
        QString actionName = attrs.value("name").toString();
        QString device = attrs.value("device").toString();

        // Skip non-keyboard bindings
        if (device != QLatin1String("Keyboard")) {
          continue;
        }

        // Check if this action maps to an AIO settings key
        QString settingsKey = kActionToSettingsKey.value(actionName);
        if (settingsKey.isEmpty()) {
          continue;
        }

        Gw2Keybind kb;
        int rawButton = attrs.value("button").toInt();
        kb.virtualKey = gw2ButtonToVk(rawButton);
        kb.modifiers = attrs.value("mod").toInt(); // 0 if absent

        if (kb.virtualKey != rawButton) {
          qInfo() << "Gw2KeybindParser: Converted" << actionName
                  << "GW2 button:" << rawButton << "→ VK:" << kb.virtualKey;
        }

        result[settingsKey] = kb;
        ++parsed;

        qInfo() << "Gw2KeybindParser:" << actionName << "→" << settingsKey
                << "VK:" << kb.virtualKey << "mod:" << kb.modifiers;
      }
    }

    if (xml.hasError()) {
      qWarning() << "Gw2KeybindParser: XML parse error:" << xml.errorString();
    }

    qInfo() << "Gw2KeybindParser: Parsed" << parsed << "keybinds from"
            << QFileInfo(xmlPath).fileName();
    return result;
  }

  /**
   * @brief Get a human-readable name for a VK code.
   * Used for display in status messages.
   */
  static QString vkToDisplayName(int vk, int mod) {
    // GW2 mod bitmask: 1=Shift, 2=Ctrl, 4=Alt
    QString name;
    if (mod & 1)
      name += "Shift+";
    if (mod & 2)
      name += "Ctrl+";
    if (mod & 4)
      name += "Alt+";

    // Common VK names
    if (vk >= 0x60 && vk <= 0x69) {
      name += "Numpad " + QString::number(vk - 0x60);
    } else if (vk >= 0x30 && vk <= 0x39) {
      name += QString::number(vk - 0x30);
    } else if (vk >= 0x41 && vk <= 0x5A) {
      name += QChar(vk);
    } else if (vk >= 0x70 && vk <= 0x7B) {
      name += "F" + QString::number(vk - 0x70 + 1);
    } else {
      name += QString("VK_%1").arg(vk);
    }
    return name;
  }
};

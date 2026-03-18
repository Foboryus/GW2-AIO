// REVIEW BEFORE BETA: emojis in default menu data (L126-174) — replace with SVG icons.
#pragma once

#include <QString>
#include <QList>
#include <QKeySequence>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief Single item in a radial menu
 */
struct RadialItem
{
    QString id;
    QString name;
    QString icon;           // Path to icon or emoji
    QString keybind;        // GW2 keybind to send (e.g., "X" for mount 1)
    QString command;        // Alternative: custom command
    bool enabled = true;
    
    QJsonObject toJson() const
    {
        return {
            {"id", id},
            {"name", name},
            {"icon", icon},
            {"keybind", keybind},
            {"command", command},
            {"enabled", enabled}
        };
    }
    
    static RadialItem fromJson(const QJsonObject& json)
    {
        RadialItem item;
        item.id = json["id"].toString();
        item.name = json["name"].toString();
        item.icon = json["icon"].toString();
        item.keybind = json["keybind"].toString();
        item.command = json["command"].toString();
        item.enabled = json["enabled"].toBool(true);
        return item;
    }
};

/**
 * @brief Condition for showing a radial menu
 */
enum class RadialCondition
{
    Always,         // Always show this menu
    InWvW,          // Only in WvW maps
    Underwater,     // Only when underwater
    OutOfCombat,    // Only when not in combat
    Mounted,        // Only when already mounted
    NotMounted      // Only when not mounted
};

/**
 * @brief A complete radial menu with items
 */
struct RadialMenu
{
    QString id;
    QString name;
    QString hotkey;         // Key to trigger (e.g., "V")
    bool holdMode = true;   // true = hold to show, false = toggle
    QList<RadialItem> items;
    RadialCondition condition = RadialCondition::Always;
    int centerDeadzone = 50;    // Pixels from center to ignore
    bool queueIfBlocked = true; // Queue selection if can't execute now
    
    QJsonObject toJson() const
    {
        QJsonArray itemsJson;
        for (const auto& item : items) {
            itemsJson.append(item.toJson());
        }
        
        return {
            {"id", id},
            {"name", name},
            {"hotkey", hotkey},
            {"holdMode", holdMode},
            {"items", itemsJson},
            {"condition", static_cast<int>(condition)},
            {"centerDeadzone", centerDeadzone},
            {"queueIfBlocked", queueIfBlocked}
        };
    }
    
    static RadialMenu fromJson(const QJsonObject& json)
    {
        RadialMenu menu;
        menu.id = json["id"].toString();
        menu.name = json["name"].toString();
        menu.hotkey = json["hotkey"].toString();
        menu.holdMode = json["holdMode"].toBool(true);
        menu.condition = static_cast<RadialCondition>(json["condition"].toInt(0));
        menu.centerDeadzone = json["centerDeadzone"].toInt(50);
        menu.queueIfBlocked = json["queueIfBlocked"].toBool(true);
        
        for (const auto& itemJson : json["items"].toArray()) {
            menu.items.append(RadialItem::fromJson(itemJson.toObject()));
        }
        
        return menu;
    }
};

/**
 * @brief Default mount radial menu (GW2Radial style)
 */
namespace DefaultMenus
{
    inline RadialMenu mounts()
    {
        RadialMenu menu;
        menu.id = "mounts";
        menu.name = "Mounts";
        menu.hotkey = "V";
        menu.holdMode = true;
        menu.condition = RadialCondition::NotMounted;
        
        menu.items = {
            {"raptor", "Raptor", "🦎", "X", "", true},
            {"springer", "Springer", "🐰", "Shift+X", "", true},
            {"skimmer", "Skimmer", "🐟", "Ctrl+X", "", true},
            {"jackal", "Jackal", "🐺", "Alt+X", "", true},
            {"griffon", "Griffon", "🦅", "Shift+Ctrl+X", "", true},
            {"rollerbeetle", "Roller Beetle", "🪲", "Shift+Alt+X", "", true},
            {"warclaw", "Warclaw", "🐱", "Ctrl+Alt+X", "", true},
            {"skyscale", "Skyscale", "🐉", "Shift+Ctrl+Alt+X", "", true}
        };
        
        return menu;
    }
    
    inline RadialMenu novelties()
    {
        RadialMenu menu;
        menu.id = "novelties";
        menu.name = "Novelties";
        menu.hotkey = "N";
        menu.holdMode = true;
        
        menu.items = {
            {"chair", "Chair", "🪑", "1", "", true},
            {"instrument", "Instrument", "🎸", "2", "", true},
            {"held", "Held Item", "🏮", "3", "", true},
            {"toy", "Toy", "🎲", "4", "", true},
            {"tonic", "Tonic", "🧪", "5", "", true}
        };
        
        return menu;
    }
    
    inline RadialMenu markers()
    {
        RadialMenu menu;
        menu.id = "markers";
        menu.name = "Squad Markers";
        menu.hotkey = "M";
        menu.holdMode = true;
        
        menu.items = {
            {"arrow", "Arrow", "➡️", "Shift+1", "", true},
            {"circle", "Circle", "⭕", "Shift+2", "", true},
            {"heart", "Heart", "❤️", "Shift+3", "", true},
            {"square", "Square", "🔲", "Shift+4", "", true},
            {"star", "Star", "⭐", "Shift+5", "", true},
            {"spiral", "Spiral", "🌀", "Shift+6", "", true},
            {"triangle", "Triangle", "🔺", "Shift+7", "", true},
            {"x", "X", "❌", "Shift+8", "", true}
        };
        
        return menu;
    }
}

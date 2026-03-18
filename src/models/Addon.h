#pragma once

#include <QString>
#include <QList>
#include <QDateTime>

/**
 * @brief Represents an addon that can be installed/managed
 */
struct AddonDefinition
{
    QString id;           // Unique identifier (e.g., "arcdps", "radial")
    QString name;         // Display name
    QString description;
    QString sourceUrl;    // GitHub repo or direct URL
    QString installPath;  // Relative path within GW2 directory
    QString filename;     // Primary file (e.g., "d3d11.dll")
    bool requiresLoader;  // Needs addon-loader chain
};

/**
 * @brief Represents an installed addon with version info
 */
struct InstalledAddon
{
    AddonDefinition definition;
    QString version;
    QString installedPath;  // Full path to installed file
    QDateTime installedAt;
    bool enabled;
    
    bool isOutdated = false;
    QString latestVersion;
};

/**
 * @brief Known addon definitions
 */
namespace KnownAddons
{
    inline AddonDefinition ArcDPS()
    {
        return {
            "arcdps",
            "ArcDPS",
            "DPS meter and combat logging",
            "https://deltaconnected.com/arcdps/x64/d3d11.dll",
            "bin64",
            "arcdps_d3d11.dll",  // Renamed for addon-loader
            true
        };
    }
    
    inline AddonDefinition Radial()
    {
        return {
            "radial",
            "GW2Radial",
            "Radial menu for mounts, novelties, markers",
            "https://github.com/Friendly0Fire/GW2Radial",
            "bin64",
            "gw2radial.dll",
            true
        };
    }
    
    inline AddonDefinition BlishHUD()
    {
        return {
            "blishhud",
            "Blish HUD",
            "Modular overlay framework with community modules",
            "https://github.com/blish-hud/Blish-HUD",
            "",  // Standalone application
            "Blish HUD.exe",
            false
        };
    }
    
    inline AddonDefinition TacO()
    {
        return {
            "taco",
            "GW2TacO",
            "Tactical overlay with markers and trails",
            "https://github.com/BoyC/GW2TacO",
            "",  // Standalone application
            "GW2TacO.exe",
            false
        };
    }
    
    inline AddonDefinition AddonLoader()
    {
        return {
            "addonloader",
            "Addon Loader",
            "Chains multiple d3d11.dll addons together",
            "https://github.com/gw2-addon-loader/loader-core",
            "bin64",
            "dxgi.dll",
            false
        };
    }
    
    inline QList<AddonDefinition> all()
    {
        return { AddonLoader(), ArcDPS(), Radial(), BlishHUD(), TacO() };
    }
}

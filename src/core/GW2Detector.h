#pragma once

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QSettings>

/**
 * @brief Detects GW2 installation path from common locations
 */
class GW2Detector
{
public:
    GW2Detector() = default;
    
    /**
     * @brief Attempts to find the GW2 installation directory
     * @return Path to GW2 directory, or empty string if not found
     */
    QString detectGW2Path() const
    {
        // Try common paths first
        for (const QString& path : COMMON_PATHS) {
            if (isValidGW2Directory(path)) {
                return path;
            }
        }
        
        // Try reading from registry (GW2 installer writes here)
        QString registryPath = getPathFromRegistry();
        if (!registryPath.isEmpty() && isValidGW2Directory(registryPath)) {
            return registryPath;
        }
        
        return QString();
    }
    
    /**
     * @brief Validates that a directory contains a valid GW2 installation
     * @param path Directory path to check
     * @return true if directory contains gw2-64.exe or Gw2-64.exe
     */
    bool isValidGW2Directory(const QString& path) const
    {
        QDir dir(path);
        if (!dir.exists()) {
            return false;
        }
        
        // Check for GW2 executable
        return dir.exists("Gw2-64.exe") || dir.exists("gw2-64.exe") || dir.exists("Gw2.exe");
    }
    
    /**
     * @brief Gets the path to GW2's bin64 directory
     * @param gw2Path Base GW2 installation path
     * @return Path to bin64 directory
     */
    QString getBin64Path(const QString& gw2Path) const
    {
        return QDir(gw2Path).filePath("bin64");
    }
    
    /**
     * @brief Gets the path to Blish-HUD modules directory
     * @return Path to Blish-HUD modules folder
     */
    QString getBlishModulesPath() const
    {
        QString documentsPath = QDir::homePath() + "/Documents/Guild Wars 2/addons/blishhud/Modules";
        return documentsPath;
    }
    
private:
    QString getPathFromRegistry() const
    {
#ifdef Q_OS_WIN
        // Try HKEY_LOCAL_MACHINE first
        QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\ArenaNet\\Guild Wars 2", 
                          QSettings::NativeFormat);
        QString path = settings.value("Path").toString();
        if (!path.isEmpty()) {
            return path;
        }
        
        // Try current user
        QSettings userSettings("HKEY_CURRENT_USER\\SOFTWARE\\ArenaNet\\Guild Wars 2", 
                              QSettings::NativeFormat);
        return userSettings.value("Path").toString();
#else
        return QString();
#endif
    }
    
    // Common GW2 installation paths
    const QStringList COMMON_PATHS = {
        "C:/Program Files (x86)/Steam/steamapps/common/Guild Wars 2",
        "C:/Program Files/Guild Wars 2",
        "C:/Games/Guild Wars 2",
        "D:/Games/Guild Wars 2",
        "D:/Program Files/Guild Wars 2",
        "D:/Steam/steamapps/common/Guild Wars 2"
    };
};

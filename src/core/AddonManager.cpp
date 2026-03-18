#include "AddonManager.h"
#include <QDebug>

AddonManager::AddonManager(QObject* parent)
    : QObject(parent)
{
}

void AddonManager::setGw2Path(const QString& path)
{
    if (m_gw2Path != path) {
        m_gw2Path = path;
        emit gw2PathChanged();
    }
}

QList<InstalledAddon> AddonManager::scanInstalledAddons()
{
    QList<InstalledAddon> installed;
    
    if (m_gw2Path.isEmpty()) {
        return installed;
    }
    
    for (const AddonDefinition& def : KnownAddons::all()) {
        QString addonPath = getAddonPath(def);
        QString enabledPath = addonPath;
        QString disabledPath = addonPath + ".disabled";
        
        bool exists = QFile::exists(enabledPath);
        bool existsDisabled = QFile::exists(disabledPath);
        
        if (exists || existsDisabled) {
            InstalledAddon addon;
            addon.definition = def;
            addon.enabled = exists;
            addon.installedPath = exists ? enabledPath : disabledPath;
            addon.installedAt = QFileInfo(addon.installedPath).lastModified();
            
            // TODO: Extract version from file metadata
            addon.version = "Unknown";
            
            installed.append(addon);
        }
    }
    
    return installed;
}

bool AddonManager::installAddon(const AddonDefinition& addon, const QString& sourcePath)
{
    if (m_gw2Path.isEmpty()) {
        qWarning() << "Cannot install addon: GW2 path not set";
        return false;
    }
    
    QString destPath = getAddonPath(addon);
    QDir destDir = QFileInfo(destPath).dir();
    
    // Create directory if needed
    if (!destDir.exists()) {
        destDir.mkpath(".");
    }
    
    // Copy file
    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }
    
    bool success = QFile::copy(sourcePath, destPath);
    
    if (success) {
        qInfo() << "Installed" << addon.name << "to" << destPath;
        emit addonInstalled(addon.id);
    } else {
        qWarning() << "Failed to install" << addon.name;
    }
    
    return success;
}

bool AddonManager::uninstallAddon(const InstalledAddon& addon)
{
    bool success = QFile::remove(addon.installedPath);
    
    if (success) {
        qInfo() << "Removed" << addon.definition.name;
        emit addonRemoved(addon.definition.id);
    }
    
    return success;
}

bool AddonManager::toggleAddon(InstalledAddon& addon, bool enabled)
{
    QString currentPath = addon.installedPath;
    QString newPath;
    
    if (enabled) {
        // Remove .disabled suffix
        if (currentPath.endsWith(".disabled")) {
            newPath = currentPath.left(currentPath.length() - 9);
        } else {
            return true; // Already enabled
        }
    } else {
        // Add .disabled suffix
        if (!currentPath.endsWith(".disabled")) {
            newPath = currentPath + ".disabled";
        } else {
            return true; // Already disabled
        }
    }
    
    bool success = QFile::rename(currentPath, newPath);
    
    if (success) {
        addon.installedPath = newPath;
        addon.enabled = enabled;
        emit addonToggled(addon.definition.id, enabled);
    }
    
    return success;
}

bool AddonManager::isAddonLoaderInstalled() const
{
    AddonDefinition loader = KnownAddons::AddonLoader();
    return addonFileExists(loader);
}

QString AddonManager::getAddonPath(const AddonDefinition& addon) const
{
    if (addon.installPath.isEmpty()) {
        // Standalone addon, install to dedicated folder
        return QDir(m_gw2Path).filePath("addons/" + addon.id + "/" + addon.filename);
    }
    return QDir(m_gw2Path).filePath(addon.installPath + "/" + addon.filename);
}

bool AddonManager::addonFileExists(const AddonDefinition& addon) const
{
    QString path = getAddonPath(addon);
    return QFile::exists(path) || QFile::exists(path + ".disabled");
}

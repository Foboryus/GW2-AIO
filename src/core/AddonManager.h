#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "models/Addon.h"

/**
 * @brief Manages addon installation, removal, and state
 */
class AddonManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString gw2Path READ gw2Path WRITE setGw2Path NOTIFY gw2PathChanged)
    
public:
    explicit AddonManager(QObject* parent = nullptr);
    
    QString gw2Path() const { return m_gw2Path; }
    void setGw2Path(const QString& path);
    
    /**
     * @brief Scan GW2 directory for installed addons
     */
    QList<InstalledAddon> scanInstalledAddons();
    
    /**
     * @brief Install an addon from a downloaded file
     */
    bool installAddon(const AddonDefinition& addon, const QString& sourcePath);
    
    /**
     * @brief Remove an addon
     */
    bool uninstallAddon(const InstalledAddon& addon);
    
    /**
     * @brief Enable or disable an addon (renames .dll to .dll.disabled)
     */
    bool toggleAddon(InstalledAddon& addon, bool enabled);
    
    /**
     * @brief Check if addon-loader is installed
     */
    bool isAddonLoaderInstalled() const;
    
signals:
    void gw2PathChanged();
    void addonInstalled(const QString& addonId);
    void addonRemoved(const QString& addonId);
    void addonToggled(const QString& addonId, bool enabled);
    
private:
    QString m_gw2Path;
    
    QString getAddonPath(const AddonDefinition& addon) const;
    bool addonFileExists(const AddonDefinition& addon) const;
};

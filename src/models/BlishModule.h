#pragma once

#include <QString>
#include <QByteArray>
#include <QJsonObject>

/**
 * @brief Represents a Blish-HUD module (.bhm file)
 */
struct BlishModule
{
    // From manifest.json
    QString name;           // Display name
    QString namespace_;     // Unique namespace identifier
    QString version;
    QString description;
    QString author;
    QString package;        // DLL filename
    
    // File info
    QString bhmPath;        // Full path to .bhm file
    QByteArray icon;        // Module icon (optional)
    
    // State
    bool enabled = true;
    
    /**
     * @brief Parse module info from manifest.json content
     */
    static BlishModule fromManifest(const QJsonObject& manifest, const QString& bhmPath)
    {
        BlishModule module;
        module.name = manifest["name"].toString();
        module.namespace_ = manifest["namespace"].toString();
        module.version = manifest["version"].toString();
        module.description = manifest["description"].toString();
        module.author = manifest["author"].toString("Unknown");
        module.package = manifest["package"].toString();
        module.bhmPath = bhmPath;
        return module;
    }
    
    /**
     * @brief Get unique identifier for this module
     */
    QString id() const
    {
        return namespace_.isEmpty() ? package : namespace_;
    }
};

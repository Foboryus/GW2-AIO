#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "models/BlishModule.h"

/**
 * @brief Manages Blish-HUD modules (.bhm files)
 */
class BlishModuleManager : public QObject
{
    Q_OBJECT
    
public:
    explicit BlishModuleManager(QObject* parent = nullptr);
    
    /**
     * @brief Get the default Blish modules directory
     */
    static QString getModulesPath();
    
    /**
     * @brief Scan modules directory for installed .bhm files
     */
    QList<BlishModule> scanModules();
    
    /**
     * @brief Parse a .bhm file and extract module info
     */
    BlishModule parseModule(const QString& bhmPath);
    
    /**
     * @brief Enable a module (rename from .bhm.disabled to .bhm)
     */
    bool enableModule(const QString& bhmPath);
    
    /**
     * @brief Disable a module (rename from .bhm to .bhm.disabled)
     */
    bool disableModule(const QString& bhmPath);
    
    /**
     * @brief Delete a module file
     */
    bool deleteModule(const QString& bhmPath);
    
signals:
    void modulesChanged();
    
private:
    QJsonObject extractManifest(const QString& bhmPath);
};

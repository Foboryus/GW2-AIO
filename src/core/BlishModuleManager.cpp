#include "BlishModuleManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>

// For ZIP extraction (using Qt built-in or QuaZip)
// Simplified implementation - in production use QuaZip
#ifdef USE_QUAZIP
#include <quazip/quazip.h>
#include <quazip/quazipfile.h>
#endif

BlishModuleManager::BlishModuleManager(QObject* parent)
    : QObject(parent)
{
}

QString BlishModuleManager::getModulesPath()
{
    QString documentsPath = QDir::homePath();
    return documentsPath + "/Documents/Guild Wars 2/addons/blishhud/Modules";
}

QList<BlishModule> BlishModuleManager::scanModules()
{
    QList<BlishModule> modules;
    
    QString modulesPath = getModulesPath();
    QDir dir(modulesPath);
    
    if (!dir.exists()) {
        qWarning() << "Blish modules directory not found:" << modulesPath;
        return modules;
    }
    
    // Scan for .bhm files
    QStringList filters = {"*.bhm", "*.bhm.disabled"};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    
    for (const QFileInfo& file : files) {
        BlishModule module = parseModule(file.absoluteFilePath());
        if (!module.name.isEmpty()) {
            module.enabled = !file.suffix().endsWith("disabled");
            modules.append(module);
        }
    }
    
    qInfo() << "Found" << modules.size() << "Blish modules";
    return modules;
}

BlishModule BlishModuleManager::parseModule(const QString& bhmPath)
{
    BlishModule module;
    module.bhmPath = bhmPath;
    
    QJsonObject manifest = extractManifest(bhmPath);
    
    if (!manifest.isEmpty()) {
        module = BlishModule::fromManifest(manifest, bhmPath);
    } else {
        // Fallback: use filename as name
        QFileInfo fi(bhmPath);
        module.name = fi.completeBaseName();
    }
    
    return module;
}

bool BlishModuleManager::enableModule(const QString& bhmPath)
{
    if (!bhmPath.endsWith(".disabled")) {
        return true; // Already enabled
    }
    
    QString newPath = bhmPath.left(bhmPath.length() - 9); // Remove ".disabled"
    bool success = QFile::rename(bhmPath, newPath);
    
    if (success) {
        emit modulesChanged();
    }
    return success;
}

bool BlishModuleManager::disableModule(const QString& bhmPath)
{
    if (bhmPath.endsWith(".disabled")) {
        return true; // Already disabled
    }
    
    QString newPath = bhmPath + ".disabled";
    bool success = QFile::rename(bhmPath, newPath);
    
    if (success) {
        emit modulesChanged();
    }
    return success;
}

bool BlishModuleManager::deleteModule(const QString& bhmPath)
{
    bool success = QFile::remove(bhmPath);
    
    if (success) {
        emit modulesChanged();
    }
    return success;
}

QJsonObject BlishModuleManager::extractManifest(const QString& bhmPath)
{
    QJsonObject manifest;
    
#ifdef USE_QUAZIP
    QuaZip zip(bhmPath);
    if (!zip.open(QuaZip::mdUnzip)) {
        qWarning() << "Failed to open .bhm file:" << bhmPath;
        return manifest;
    }
    
    // Look for manifest.json
    if (zip.setCurrentFile("manifest.json")) {
        QuaZipFile file(&zip);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            manifest = doc.object();
            file.close();
        }
    }
    
    zip.close();
#else
    // Simplified: .bhm is a ZIP, we'd need QuaZip or similar
    // For now, just return empty and use filename-based fallback
    Q_UNUSED(bhmPath);
    qDebug() << "QuaZip not available, using filename fallback for module parsing";
#endif
    
    return manifest;
}

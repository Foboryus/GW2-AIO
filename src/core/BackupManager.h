#pragma once

#include <QObject>
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief Settings backup and restore
 */
class BackupManager : public QObject
{
    Q_OBJECT
    
public:
    explicit BackupManager(const QString& dataDir, QObject* parent = nullptr);
    
    /**
     * @brief Create a full backup
     */
    bool createBackup(const QString& name = QString());
    
    /**
     * @brief List available backups
     */
    struct BackupInfo {
        QString name;
        QString path;
        QDateTime created;
        qint64 size;
    };
    QList<BackupInfo> listBackups() const;
    
    /**
     * @brief Restore from backup
     */
    bool restoreBackup(const QString& name);
    
    /**
     * @brief Delete a backup
     */
    bool deleteBackup(const QString& name);
    
    /**
     * @brief Auto-backup settings
     */
    void setAutoBackupEnabled(bool enabled) { m_autoBackup = enabled; }
    bool autoBackupEnabled() const { return m_autoBackup; }
    void setMaxBackups(int max) { m_maxBackups = max; }
    
signals:
    void backupCreated(const QString& path);
    void backupRestored(const QString& name);
    void error(const QString& message);
    
private:
    void pruneOldBackups();
    QString generateBackupName() const;
    
    QString m_dataDir;
    QString m_backupDir;
    bool m_autoBackup = true;
    int m_maxBackups = 10;
};

// Implementation
inline BackupManager::BackupManager(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_backupDir(QDir(dataDir).filePath("backups"))
{
    QDir().mkpath(m_backupDir);
}

inline bool BackupManager::createBackup(const QString& name)
{
    QString backupName = name.isEmpty() ? generateBackupName() : name;
    QString backupPath = QDir(m_backupDir).filePath(backupName);
    
    QDir().mkpath(backupPath);
    
    // Files to backup
    QStringList files = {
        "settings.ini",
        "radial_config.json",
        "module_settings.json"
    };
    
    for (const QString& file : files) {
        QString srcPath = QDir(m_dataDir).filePath(file);
        QString dstPath = QDir(backupPath).filePath(file);
        
        if (QFile::exists(srcPath)) {
            QFile::copy(srcPath, dstPath);
        }
    }
    
    // Create manifest
    QJsonObject manifest;
    manifest["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    manifest["version"] = "1.0";
    manifest["files"] = QJsonArray::fromStringList(files);
    
    QFile manifestFile(QDir(backupPath).filePath("manifest.json"));
    if (manifestFile.open(QIODevice::WriteOnly)) {
        manifestFile.write(QJsonDocument(manifest).toJson());
        manifestFile.close();
    }
    
    pruneOldBackups();
    
    emit backupCreated(backupPath);
    qInfo() << "Backup created:" << backupName;
    
    return true;
}

inline QList<BackupManager::BackupInfo> BackupManager::listBackups() const
{
    QList<BackupInfo> backups;
    
    QDir dir(m_backupDir);
    for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        QString path = dir.filePath(entry);
        QString manifestPath = QDir(path).filePath("manifest.json");
        
        BackupInfo info;
        info.name = entry;
        info.path = path;
        
        QFile manifestFile(manifestPath);
        if (manifestFile.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
            info.created = QDateTime::fromString(doc.object()["created"].toString(), Qt::ISODate);
        } else {
            info.created = QFileInfo(path).birthTime();
        }
        
        // Calculate size
        info.size = 0;
        QDir backupDir(path);
        for (const QFileInfo& fileInfo : backupDir.entryInfoList(QDir::Files)) {
            info.size += fileInfo.size();
        }
        
        backups.append(info);
    }
    
    return backups;
}

inline bool BackupManager::restoreBackup(const QString& name)
{
    QString backupPath = QDir(m_backupDir).filePath(name);
    
    if (!QDir(backupPath).exists()) {
        emit error("Backup not found: " + name);
        return false;
    }
    
    // First, create a backup of current state
    createBackup("pre_restore_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    
    // Restore files
    QDir backupDir(backupPath);
    for (const QString& file : backupDir.entryList(QDir::Files)) {
        if (file == "manifest.json") continue;
        
        QString srcPath = backupDir.filePath(file);
        QString dstPath = QDir(m_dataDir).filePath(file);
        
        QFile::remove(dstPath);
        QFile::copy(srcPath, dstPath);
    }
    
    emit backupRestored(name);
    qInfo() << "Backup restored:" << name;
    
    return true;
}

inline bool BackupManager::deleteBackup(const QString& name)
{
    QString backupPath = QDir(m_backupDir).filePath(name);
    
    QDir dir(backupPath);
    if (!dir.exists()) return false;
    
    return dir.removeRecursively();
}

inline void BackupManager::pruneOldBackups()
{
    QList<BackupInfo> backups = listBackups();
    
    // Sort by date (oldest first)
    std::sort(backups.begin(), backups.end(), [](const BackupInfo& a, const BackupInfo& b) {
        return a.created < b.created;
    });
    
    // Delete oldest if over limit
    while (backups.size() > m_maxBackups) {
        deleteBackup(backups.first().name);
        backups.removeFirst();
    }
}

inline QString BackupManager::generateBackupName() const
{
    return "backup_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
}

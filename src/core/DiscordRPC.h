// REVIEW BEFORE BETA: all inline — split to .h/.cpp pair. DEV LOG at L203.
#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QLibrary>

/**
 * @brief Discord Rich Presence integration
 * 
 * Shows "Playing Guild Wars 2" with character/map info in Discord.
 * Uses discord-rpc library (if available).
 */
class DiscordRPC : public QObject
{
    Q_OBJECT
    
public:
    explicit DiscordRPC(QObject* parent = nullptr);
    ~DiscordRPC();
    
    /**
     * @brief Initialize Discord RPC
     */
    bool initialize();
    
    /**
     * @brief Check if Discord is connected
     */
    bool isConnected() const { return m_connected; }
    
    /**
     * @brief Enable/disable
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    
    /**
     * @brief Update presence
     */
    void setPresence(const QString& state, const QString& details = QString(),
                     const QString& largeImage = "gw2_logo",
                     const QString& smallImage = QString());
    
    /**
     * @brief Set character info
     */
    void setCharacter(const QString& name, const QString& profession, int level);
    
    /**
     * @brief Set map info
     */
    void setMap(const QString& mapName, int mapId = 0);
    
    /**
     * @brief Clear presence
     */
    void clearPresence();
    
signals:
    void connected();
    void disconnected();
    void error(const QString& message);
    
private slots:
    void updatePresence();
    
private:
    bool loadDiscordLibrary();
    
    QLibrary* m_discordLib = nullptr;
    QTimer* m_updateTimer;
    
    bool m_enabled = true;
    bool m_connected = false;
    bool m_initialized = false;
    
    // Presence data
    QString m_state;
    QString m_details;
    QString m_largeImage;
    QString m_smallImage;
    QString m_characterName;
    QString m_profession;
    QString m_mapName;
    qint64 m_startTime;
    
    // Discord Application ID
    static constexpr const char* APP_ID = "GW2AIO";  // Would need real ID
};

// Implementation
inline DiscordRPC::DiscordRPC(QObject* parent)
    : QObject(parent)
    , m_updateTimer(new QTimer(this))
    , m_startTime(QDateTime::currentSecsSinceEpoch())
{
    connect(m_updateTimer, &QTimer::timeout, this, &DiscordRPC::updatePresence);
}

inline DiscordRPC::~DiscordRPC()
{
    if (m_initialized) {
        clearPresence();
    }
    delete m_discordLib;
}

inline bool DiscordRPC::initialize()
{
    if (!loadDiscordLibrary()) {
        qInfo() << "Discord RPC not available (library not found)";
        return false;
    }
    
    // Would call Discord_Initialize here
    m_initialized = true;
    m_connected = true;
    
    m_updateTimer->start(15000);  // Update every 15 seconds
    
    emit connected();
    qInfo() << "Discord RPC initialized";
    
    return true;
}

inline bool DiscordRPC::loadDiscordLibrary()
{
    // Try to load discord-rpc.dll
    m_discordLib = new QLibrary("discord-rpc");
    
    if (!m_discordLib->load()) {
        // Also try in app directory
        QString appDir = QCoreApplication::applicationDirPath();
        m_discordLib->setFileName(QDir(appDir).filePath("discord-rpc"));
        
        if (!m_discordLib->load()) {
            delete m_discordLib;
            m_discordLib = nullptr;
            return false;
        }
    }
    
    return true;
}

inline void DiscordRPC::setEnabled(bool enabled)
{
    m_enabled = enabled;
    
    if (!enabled) {
        clearPresence();
        m_updateTimer->stop();
    } else if (m_initialized) {
        m_updateTimer->start(15000);
        updatePresence();
    }
}

inline void DiscordRPC::setPresence(const QString& state, const QString& details,
                                     const QString& largeImage, const QString& smallImage)
{
    m_state = state;
    m_details = details;
    m_largeImage = largeImage;
    m_smallImage = smallImage;
    
    if (m_enabled && m_initialized) {
        updatePresence();
    }
}

inline void DiscordRPC::setCharacter(const QString& name, const QString& profession, int level)
{
    m_characterName = name;
    m_profession = profession;
    
    m_details = QString("%1 - Level %2 %3").arg(name).arg(level).arg(profession);
    
    // Map profession to small image
    m_smallImage = profession.toLower();
}

inline void DiscordRPC::setMap(const QString& mapName, int mapId)
{
    Q_UNUSED(mapId);
    m_mapName = mapName;
    m_state = "In " + mapName;
}

inline void DiscordRPC::updatePresence()
{
    if (!m_enabled || !m_initialized) return;
    
    // Would call Discord_UpdatePresence here with:
    // - state: m_state
    // - details: m_details
    // - largeImageKey: m_largeImage
    // - smallImageKey: m_smallImage
    // - startTimestamp: m_startTime
    
    qDebug() << "Discord RPC Update:" << m_details << "|" << m_state;
}

inline void DiscordRPC::clearPresence()
{
    if (!m_initialized) return;
    
    // Would call Discord_ClearPresence here
    m_state.clear();
    m_details.clear();
}

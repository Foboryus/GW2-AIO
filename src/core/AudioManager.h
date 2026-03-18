#pragma once

#include <QObject>
#include <QString>
#include <QMap>

// Note: Audio playback requires Qt Multimedia which is optional.
// This is a stub implementation - install qtmultimedia for full audio support.

/**
 * @brief Audio manager for sound effects and notifications
 * 
 * This is a stub - Qt Multimedia not installed.
 * Audio will be logged but not played.
 */
class AudioManager : public QObject
{
    Q_OBJECT
    
public:
    static AudioManager& instance()
    {
        static AudioManager instance;
        return instance;
    }
    
    bool initialize() { return true; }
    
    enum class SoundType {
        UIClick,
        UIHover,
        Notification,
        Warning,
        Error,
        Success,
        RadialOpen,
        RadialSelect,
        MarkerReached,
        CombatStart,
        CombatEnd
    };
    Q_ENUM(SoundType)
    
    void play(SoundType type)
    {
        if (!m_enabled) return;
        qDebug() << "Audio stub: would play" << static_cast<int>(type);
    }
    
    void playFile(const QString& path)
    {
        if (!m_enabled) return;
        qDebug() << "Audio stub: would play file" << path;
    }
    
    void setVolume(float volume) { m_volume = qBound(0.0f, volume, 1.0f); }
    float volume() const { return m_volume; }
    
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
    void setCustomSound(SoundType type, const QString& path)
    {
        m_customPaths[type] = path;
    }
    
private:
    AudioManager() = default;
    
    float m_volume = 0.5f;
    bool m_enabled = true;
    QMap<SoundType, QString> m_customPaths;
};

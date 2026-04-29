#pragma once

#include <QString>
#include <QDateTime>
#include <QList>

/**
 * @brief Represents a single combat encounter
 */
struct CombatEncounter
{
    QDateTime startTime;
    QDateTime endTime;
    int durationMs = 0;
    
    // Damage stats (estimated from Mumble or ArcDPS)
    double totalDamage = 0;
    double dps = 0;           // Damage per second
    int hitCount = 0;
    
    // Target info
    QString targetName;
    uint32_t targetId = 0;
    
    // Player state
    QString characterName;
    int profession = 0;       // 1-9 for GW2 classes
    
    bool isActive() const { return endTime.isNull(); }
    
    void updateDPS()
    {
        if (durationMs > 0) {
            dps = (totalDamage / durationMs) * 1000.0;
        }
    }
};

/**
 * @brief Combat log entry
 */
struct CombatEvent
{
    enum Type {
        DamageDealt,
        DamageTaken,
        Heal,
        BuffApplied,
        BuffRemoved,
        SkillUsed,
        CombatEnter,
        CombatExit
    };
    
    QDateTime timestamp;
    Type type;
    double value = 0;
    QString skillName;
    uint32_t skillId = 0;
    QString targetName;
};

/**
 * @brief Player combat statistics
 */
struct CombatStats
{
    // Current encounter
    double currentDPS = 0;
    double peakDPS = 0;
    double totalDamage = 0;
    int duration = 0;         // seconds
    
    // Session totals
    int encounterCount = 0;
    double sessionDamage = 0;
    double sessionTime = 0;   // seconds
    double avgDPS = 0;
    
    // Recent history (for graph)
    QList<double> dpsHistory;  // Last 60 seconds, 1 sample/sec
    
    void reset()
    {
        currentDPS = 0;
        peakDPS = 0;
        totalDamage = 0;
        duration = 0;
        dpsHistory.clear();
    }
    
    void addDPSSample(double dps)
    {
        dpsHistory.append(dps);
        if (dpsHistory.size() > 60) {
            dpsHistory.removeFirst();
        }
        if (dps > peakDPS) {
            peakDPS = dps;
        }
    }
};

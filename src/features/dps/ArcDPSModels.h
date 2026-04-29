#pragma once

#include <QString>
#include <QList>
#include <QDateTime>
#include <QDataStream>
#include <cstdint>

/**
 * @brief ArcDPS EVTC log format structures
 * 
 * Based on ArcDPS EVTC documentation:
 * https://www.deltaconnected.com/arcdps/evtc/README.txt
 */
namespace ArcDPS
{

// EVTC header
struct EVTCHeader
{
    char signature[4];      // "EVTC"
    char version[8];        // e.g., "20210101"
    char revision;          // Usually 1
    uint32_t targetId;      // Boss ID or 0
    char padding[3];
};

// Agent entry (player, NPC, gadget)
struct Agent
{
    uint64_t addr;          // Unique identifier
    uint32_t professionId;  // Profession + elite spec
    uint32_t isElite;       // Elite specialization indicator
    int16_t toughness;
    int16_t concentration;
    int16_t healing;
    int16_t hitboxWidth;
    int16_t condition;
    int16_t hitboxHeight;
    char name[68];          // Account + character name
};

// Skill entry
struct Skill
{
    int32_t id;
    char name[64];
};

// Combat event
struct CombatEvent
{
    uint64_t time;          // Timestamp (ms since log start)
    uint64_t srcAgent;      // Source agent address
    uint64_t dstAgent;      // Destination agent address
    int32_t value;          // Damage or buff value
    int32_t buffDamage;     // Buff damage
    uint32_t overStackValue;
    uint32_t skillId;
    uint16_t srcInstid;     // Source instance ID
    uint16_t dstInstid;     // Destination instance ID
    uint16_t srcMasterInstid;
    uint16_t dstMasterInstid;
    uint8_t iff;            // Friend/foe
    uint8_t buff;           // Is buff
    uint8_t result;         // Strike result
    uint8_t isActivation;
    uint8_t isBuffRemove;
    uint8_t isNinety;
    uint8_t isFifty;
    uint8_t isMoving;
    uint8_t isStateChange;
    uint8_t isFlanking;
    uint8_t isShields;
    uint8_t isOffCycle;
    uint8_t padding[4];
};

// State change types
enum class StateChange : uint8_t
{
    None = 0,
    EnterCombat = 1,
    ExitCombat = 2,
    ChangeUp = 3,
    ChangeDead = 4,
    ChangeDown = 5,
    Spawn = 6,
    Despawn = 7,
    HealthUpdate = 8,
    LogStart = 9,
    LogEnd = 10,
    WeaponSwap = 11,
    MaxHealthUpdate = 12,
    PointOfView = 13,
    Language = 14,
    GWBuild = 15,
    ShardId = 16,
    Reward = 17,
    BuffInitial = 18,
    Position = 19,
    Velocity = 20,
    Facing = 21,
    TeamChange = 22,
    AttackTarget = 23,
    Targetable = 24,
    MapId = 25,
    ReplInfo = 26,
    StackActive = 27,
    StackReset = 28,
    Guild = 29,
    BuffInfo = 30,
    BuffFormula = 31,
    SkillInfo = 32,
    SkillTiming = 33,
    BreakbarState = 34,
    BreakbarPercent = 35,
    Error = 36,
    Tag = 37,
    BarrierUpdate = 38,
    StatReset = 39,
    Extension = 40,
    APIDelayed = 41
};

// Strike result types
enum class StrikeResult : uint8_t
{
    Normal = 0,
    Crit = 1,
    Glance = 2,
    Block = 3,
    Evade = 4,
    Interrupt = 5,
    Absorb = 6,
    Blind = 7,
    KillingBlow = 8,
    Downed = 9
};

// Profession IDs
enum Profession
{
    Guardian = 1,
    Warrior = 2,
    Engineer = 3,
    Ranger = 4,
    Thief = 5,
    Elementalist = 6,
    Mesmer = 7,
    Necromancer = 8,
    Revenant = 9
};

/**
 * @brief Parsed combat log data
 */
struct ParsedLog
{
    QString filename;
    QDateTime timestamp;
    
    // Header info
    QString version;
    uint32_t targetId = 0;
    QString targetName;
    
    // Agents
    QList<Agent> agents;
    QList<Skill> skills;
    QList<CombatEvent> events;
    
    // Computed stats
    uint64_t duration = 0;     // ms
    uint64_t totalDamage = 0;
    double dps = 0;
    
    // Player breakdown
    struct PlayerStats {
        QString name;
        QString accountName;
        uint32_t profession;
        uint64_t damage;
        double dps;
        int downCount;
        int deathCount;
    };
    QList<PlayerStats> playerStats;
};

} // namespace ArcDPS

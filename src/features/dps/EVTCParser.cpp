/**
 * @file EVTCParser.cpp
 * @brief Parses ArcDPS EVTC log files
 *
 * DO NOT ADD:
 * - Real-time DPS tracking (belongs in ArcDPSRealTime)
 * - UI code
 */

#include "EVTCParser.h"

#include <QDebug>
#include <algorithm>

namespace ArcDPS {

EVTCParser::EVTCParser(QObject *parent) : QObject(parent) {}

ParsedLog EVTCParser::parse(const QString &filePath) {
  ParsedLog log;
  log.filename = QFileInfo(filePath).fileName();

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    m_lastError = "Failed to open file: " + filePath;
    return log;
  }

  // Check if it's a ZIP file (evtc.zip)
  QByteArray magic = file.peek(2);
  if (magic == "PK") {
    // TODO: Handle ZIP extraction
    m_lastError = "ZIP EVTC files not yet supported - use .evtc";
    file.close();
    return log;
  }

  QDataStream stream(&file);
  stream.setByteOrder(QDataStream::LittleEndian);

  // Read header
  if (!readHeader(stream, log)) {
    file.close();
    return log;
  }

  // Read agents
  if (!readAgents(stream, log)) {
    file.close();
    return log;
  }

  // Read skills
  if (!readSkills(stream, log)) {
    file.close();
    return log;
  }

  // Read combat events
  if (!readEvents(stream, log)) {
    file.close();
    return log;
  }

  file.close();

  // Compute statistics
  computeStats(log);

  qInfo() << "Parsed EVTC:" << log.filename
          << "Duration:" << log.duration / 1000 << "s"
          << "DPS:" << log.dps;

  return log;
}

bool EVTCParser::readHeader(QDataStream &stream, ParsedLog &log) {
  EVTCHeader header;

  stream.readRawData(header.signature, 4);
  stream.readRawData(header.version, 8);
  stream >> header.revision;
  stream >> header.targetId;
  stream.readRawData(header.padding, 3);

  // Validate signature
  if (strncmp(header.signature, "EVTC", 4) != 0) {
    m_lastError = "Invalid EVTC signature";
    return false;
  }

  log.version = QString::fromLatin1(header.version, 8).trimmed();
  log.targetId = header.targetId;

  return true;
}

bool EVTCParser::readAgents(QDataStream &stream, ParsedLog &log) {
  uint32_t agentCount;
  stream >> agentCount;

  for (uint32_t i = 0; i < agentCount; i++) {
    Agent agent;

    stream >> agent.addr;
    stream >> agent.professionId;
    stream >> agent.isElite;
    stream >> agent.toughness;
    stream >> agent.concentration;
    stream >> agent.healing;
    stream >> agent.hitboxWidth;
    stream >> agent.condition;
    stream >> agent.hitboxHeight;
    stream.readRawData(agent.name, 68);

    log.agents.append(agent);

    // Check if this is the target
    if (agent.addr == log.targetId ||
        (log.targetName.isEmpty() && agent.professionId == 0xFFFF)) {
      log.targetName =
          QString::fromUtf8(agent.name).split(QString(QChar('\0'))).first();
    }
  }

  return true;
}

bool EVTCParser::readSkills(QDataStream &stream, ParsedLog &log) {
  uint32_t skillCount;
  stream >> skillCount;

  for (uint32_t i = 0; i < skillCount; i++) {
    Skill skill;

    stream >> skill.id;
    stream.readRawData(skill.name, 64);

    log.skills.append(skill);
  }

  return true;
}

bool EVTCParser::readEvents(QDataStream &stream, ParsedLog &log) {
  while (!stream.atEnd()) {
    CombatEvent event;

    stream >> event.time;
    stream >> event.srcAgent;
    stream >> event.dstAgent;
    stream >> event.value;
    stream >> event.buffDamage;
    stream >> event.overStackValue;
    stream >> event.skillId;
    stream >> event.srcInstid;
    stream >> event.dstInstid;
    stream >> event.srcMasterInstid;
    stream >> event.dstMasterInstid;
    stream >> event.iff;
    stream >> event.buff;
    stream >> event.result;
    stream >> event.isActivation;
    stream >> event.isBuffRemove;
    stream >> event.isNinety;
    stream >> event.isFifty;
    stream >> event.isMoving;
    stream >> event.isStateChange;
    stream >> event.isFlanking;
    stream >> event.isShields;
    stream >> event.isOffCycle;
    stream.readRawData(reinterpret_cast<char *>(event.padding), 4);

    log.events.append(event);
  }

  return true;
}

void EVTCParser::computeStats(ParsedLog &log) {
  if (log.events.isEmpty())
    return;

  // Find log start/end times
  uint64_t startTime = 0;
  uint64_t endTime = 0;

  for (const CombatEvent &event : log.events) {
    auto state = static_cast<StateChange>(event.isStateChange);

    if (state == StateChange::LogStart) {
      startTime = event.time;
    } else if (state == StateChange::LogEnd) {
      endTime = event.time;
    }
  }

  if (endTime > startTime) {
    log.duration = endTime - startTime;
  }

  // Calculate total damage and per-player stats
  QMap<uint64_t, ParsedLog::PlayerStats> playerMap;

  // Initialize player map from agents
  for (const Agent &agent : log.agents) {
    // Players have profession < 10
    if (agent.professionId > 0 && agent.professionId <= 9) {
      ParsedLog::PlayerStats stats;
      QString fullName = QString::fromUtf8(agent.name);
      QStringList parts = fullName.split(QString(QChar('\0')));
      stats.name = parts.value(0);
      stats.accountName = parts.value(1);
      stats.profession = agent.professionId;
      stats.damage = 0;
      stats.dps = 0;
      stats.downCount = 0;
      stats.deathCount = 0;

      playerMap[agent.addr] = stats;
    }
  }

  // Process events
  for (const CombatEvent &event : log.events) {
    auto state = static_cast<StateChange>(event.isStateChange);

    // Skip state change events for damage calculation
    if (state != StateChange::None) {
      // Track downs and deaths
      if (state == StateChange::ChangeDown) {
        if (playerMap.contains(event.srcAgent)) {
          playerMap[event.srcAgent].downCount++;
        }
      } else if (state == StateChange::ChangeDead) {
        if (playerMap.contains(event.srcAgent)) {
          playerMap[event.srcAgent].deathCount++;
        }
      }
      continue;
    }

    // Calculate damage
    int32_t damage = 0;

    if (event.buff == 0 && event.value > 0) {
      // Direct damage
      damage = event.value;
    } else if (event.buff == 1 && event.buffDamage > 0) {
      // Condition damage
      damage = event.buffDamage;
    }

    if (damage > 0) {
      log.totalDamage += damage;

      if (playerMap.contains(event.srcAgent)) {
        playerMap[event.srcAgent].damage += damage;
      }
    }
  }

  // Calculate DPS
  if (log.duration > 0) {
    log.dps = static_cast<double>(log.totalDamage) / (log.duration / 1000.0);

    for (auto &stats : playerMap) {
      stats.dps = static_cast<double>(stats.damage) / (log.duration / 1000.0);
    }
  }

  // Convert map to list
  log.playerStats = playerMap.values();

  // Sort by DPS descending
  std::sort(log.playerStats.begin(), log.playerStats.end(),
            [](const auto &a, const auto &b) { return a.dps > b.dps; });
}

} // namespace ArcDPS

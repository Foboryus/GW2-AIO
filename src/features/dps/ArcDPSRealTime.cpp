/**
 * @file ArcDPSRealTime.cpp
 * @brief Real-time integration with ArcDPS via shared memory
 *
 * ArcDPS exposes combat data through shared memory when using certain plugins.
 * This provides real-time DPS data without parsing logs.
 *
 * NOTE: Requires ArcDPS extras plugin for full functionality.
 *
 * DO NOT ADD:
 * - Log parsing (belongs in EVTCParser)
 * - UI code
 */

#include "ArcDPSRealTime.h"

#include <QDebug>
#include <QJsonArray>

namespace ArcDPS {

RealTimeIntegration::RealTimeIntegration(QObject *parent)
    : QObject(parent), m_pollTimer(new QTimer(this)),
      m_sharedMem(new QSharedMemory(ARCDPS_SHMEM_KEY, this)) {
  connect(m_pollTimer, &QTimer::timeout, this, &RealTimeIntegration::pollData);
}

RealTimeIntegration::~RealTimeIntegration() { stop(); }

bool RealTimeIntegration::start() {
  if (!connectToArcDPS()) {
    qInfo() << "ArcDPS real-time not available - will use log parsing";
    return false;
  }

  // Poll at 100ms intervals
  m_pollTimer->start(100);

  qInfo() << "ArcDPS real-time integration started";
  return true;
}

void RealTimeIntegration::stop() {
  m_pollTimer->stop();

  if (m_sharedMem->isAttached()) {
    m_sharedMem->detach();
  }
}

bool RealTimeIntegration::connectToArcDPS() {
  // Try to attach to ArcDPS shared memory
  if (m_sharedMem->attach(QSharedMemory::ReadOnly)) {
    m_available = true;
    emit availabilityChanged(true);
    return true;
  }

  // Try extras key
  m_sharedMem->setKey(ARCDPS_EXTRAS_KEY);
  if (m_sharedMem->attach(QSharedMemory::ReadOnly)) {
    m_available = true;
    emit availabilityChanged(true);
    return true;
  }

  m_available = false;
  return false;
}

void RealTimeIntegration::pollData() {
  if (!m_sharedMem->isAttached()) {
    // Try to reconnect
    if (!connectToArcDPS()) {
      return;
    }
  }

  parseSharedData();

  // Emit combat state changes
  if (m_encounter.inCombat && !m_wasInCombat) {
    emit combatStarted();
  } else if (!m_encounter.inCombat && m_wasInCombat) {
    emit combatEnded();
  }
  m_wasInCombat = m_encounter.inCombat;

  emit dataUpdated(m_encounter);
}

void RealTimeIntegration::parseSharedData() {
  if (!m_sharedMem->lock())
    return;

  // Read shared memory data
  // Note: Actual format depends on ArcDPS plugin implementation
  // This is a placeholder showing expected structure

  const char *data = static_cast<const char *>(m_sharedMem->constData());
  int size = m_sharedMem->size();

  if (size > 0 && data) {
    // Try to parse as JSON (common format for extras plugins)
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(data, size));

    if (doc.isObject()) {
      QJsonObject root = doc.object();

      m_encounter.inCombat = root["in_combat"].toBool();
      m_encounter.duration = root["duration"].toInteger();
      m_encounter.targetName = root["target"].toString();
      m_encounter.targetHealth = root["target_health"].toDouble(1.0);

      // Parse player data
      m_encounter.players.clear();

      QJsonArray players = root["players"].toArray();
      for (const QJsonValue &pVal : players) {
        QJsonObject p = pVal.toObject();

        EncounterData::PlayerData player;
        player.name = p["name"].toString();
        player.accountName = p["account"].toString();
        player.profession = p["profession"].toInt();
        player.damage = p["damage"].toInteger();
        player.dps = p["dps"].toDouble();
        player.hits = p["hits"].toInt();
        player.crits = p["crits"].toInt();
        player.critRate =
            player.hits > 0 ? float(player.crits) / float(player.hits) : 0.0f;

        m_encounter.players.append(player);

        if (p["is_self"].toBool()) {
          m_encounter.self = player;
        }
      }

      // Calculate group DPS
      m_encounter.totalGroupDPS = 0;
      for (const auto &p : m_encounter.players) {
        m_encounter.totalGroupDPS += p.dps;
      }
    }
  }

  m_sharedMem->unlock();
}

} // namespace ArcDPS

/**
 * @file CombatTracker.cpp
 * @brief Tracks combat state using Mumble Link API
 *
 * The Mumble Link provides limited combat info, but we can detect:
 * - Combat state from UI flags
 * - Position changes (movement = likely in combat)
 * - Map changes (resets encounter)
 *
 * DO NOT ADD:
 * - ArcDPS parsing (belongs in ArcDPSRealTime)
 * - UI code
 */

#include "CombatTracker.h"

#include <QDebug>

CombatTracker::CombatTracker(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumble(mumble), m_updateTimer(new QTimer(this)) {
  connect(m_updateTimer, &QTimer::timeout, this, &CombatTracker::updateStats);
  connect(m_mumble, &MumbleLink::mapChanged, this,
          &CombatTracker::onMapChanged);
  connect(m_mumble, &MumbleLink::positionChanged, this,
          &CombatTracker::onMumbleUpdate);
}

void CombatTracker::start() {
  m_updateTimer->start(1000); // Update every second
  qInfo() << "Combat tracker started";
}

void CombatTracker::stop() {
  m_updateTimer->stop();
  if (m_inCombat) {
    exitCombat();
  }
}

void CombatTracker::setMumbleLink(MumbleLink *mumble) {
  if (m_mumble == mumble) return;

  if (m_mumble) {
    disconnect(m_mumble, &MumbleLink::mapChanged, this,
               &CombatTracker::onMapChanged);
    disconnect(m_mumble, &MumbleLink::positionChanged, this,
               &CombatTracker::onMumbleUpdate);
  }

  m_mumble = mumble;

  if (m_mumble) {
    connect(m_mumble, &MumbleLink::mapChanged, this,
            &CombatTracker::onMapChanged);
    connect(m_mumble, &MumbleLink::positionChanged, this,
            &CombatTracker::onMumbleUpdate);
  } else {
    // Failsafe: exit combat if connection lost
    if (m_inCombat) {
      exitCombat();
    }
  }
}

void CombatTracker::resetEncounter() {
  m_stats.reset();
  m_estimatedDamage = 0;
  emit statsUpdated();
}

void CombatTracker::addDamage(double amount, const QString &skillName) {
  Q_UNUSED(skillName);

  if (!m_inCombat) {
    enterCombat();
  }

  m_estimatedDamage += amount;
  m_currentEncounter.totalDamage += amount;
  m_currentEncounter.hitCount++;

  emit statsUpdated();
}

void CombatTracker::onMumbleUpdate() { detectCombatFromMumble(); }

void CombatTracker::onMapChanged(uint32_t mapId) {
  Q_UNUSED(mapId);

  // Map change = end encounter
  if (m_inCombat) {
    exitCombat();
  }
  resetEncounter();
}

void CombatTracker::updateStats() {
  if (!m_inCombat) {
    return;
  }

  // Update duration
  m_stats.duration = m_combatTimer.elapsed() / 1000;
  m_currentEncounter.durationMs = m_combatTimer.elapsed();

  // Calculate DPS
  if (m_stats.duration > 0) {
    m_stats.totalDamage = m_currentEncounter.totalDamage;
    m_stats.currentDPS = m_stats.totalDamage / m_stats.duration;
    m_currentEncounter.updateDPS();
  }

  // Add to history for graph
  m_stats.addDPSSample(m_stats.currentDPS);

  emit statsUpdated();
}

void CombatTracker::enterCombat() {
  if (m_inCombat)
    return;

  m_inCombat = true;
  m_combatTimer.start();

  m_currentEncounter = CombatEncounter();
  m_currentEncounter.startTime = QDateTime::currentDateTime();
  m_currentEncounter.characterName = m_mumble->characterName();

  emit combatStateChanged(true);
  emit encounterStarted();

  qInfo() << "Entered combat";
}

void CombatTracker::exitCombat() {
  if (!m_inCombat)
    return;

  m_inCombat = false;

  m_currentEncounter.endTime = QDateTime::currentDateTime();
  m_currentEncounter.durationMs = m_combatTimer.elapsed();
  m_currentEncounter.updateDPS();

  // Save to history
  if (m_currentEncounter.durationMs > 3000) { // Only save if > 3 seconds
    m_encounterHistory.append(m_currentEncounter);
    m_stats.encounterCount++;
    m_stats.sessionDamage += m_currentEncounter.totalDamage;
    m_stats.sessionTime += m_currentEncounter.durationMs / 1000.0;
    if (m_stats.sessionTime > 0) {
      m_stats.avgDPS = m_stats.sessionDamage / m_stats.sessionTime;
    }
  }

  emit combatStateChanged(false);
  emit encounterEnded(m_currentEncounter);

  qInfo() << "Exited combat - DPS:" << m_currentEncounter.dps
          << "Duration:" << m_currentEncounter.durationMs / 1000.0 << "s";
}

void CombatTracker::detectCombatFromMumble() {
  if (!m_mumble) return;

  // Get current position
  float x = m_mumble->playerX();
  float y = m_mumble->playerY();
  float z = m_mumble->playerZ();

  // Calculate movement
  float dx = x - m_lastX;
  float dy = y - m_lastY;
  float dz = z - m_lastZ;
  float movement = std::sqrt(dx * dx + dy * dy + dz * dz);

  m_lastX = x;
  m_lastY = y;
  m_lastZ = z;

  // Very rough heuristic: rapid movement may indicate combat
  // This is NOT accurate - real combat detection needs ArcDPS
  // For now, we mainly rely on manual enterCombat/exitCombat or UI

  if (movement < 0.1f) {
    m_stationaryFrames++;
    // After 10 seconds stationary, exit combat
    if (m_stationaryFrames > 200 && m_inCombat) { // 200 * 50ms = 10s
      exitCombat();
    }
  } else {
    m_stationaryFrames = 0;
  }
}

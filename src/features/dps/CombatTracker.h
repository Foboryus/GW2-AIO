#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <cmath>


#include "CombatModels.h"
#include "core/MumbleLink.h"

/**
 * @brief Tracks combat state using Mumble Link API
 *
 * The Mumble Link provides limited combat info, but we can detect:
 * - Combat state from UI flags
 * - Position changes (movement = likely in combat)
 * - Map changes (resets encounter)
 *
 * DO NOT ADD:
 * - Inline implementations (use CombatTracker.cpp)
 */
class CombatTracker : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool inCombat READ isInCombat NOTIFY combatStateChanged)
  Q_PROPERTY(double currentDPS READ currentDPS NOTIFY statsUpdated)
  Q_PROPERTY(int combatDuration READ combatDuration NOTIFY statsUpdated)

public:
  explicit CombatTracker(MumbleLink *mumble, QObject *parent = nullptr);

  // State
  bool isInCombat() const { return m_inCombat; }
  double currentDPS() const { return m_stats.currentDPS; }
  int combatDuration() const { return m_stats.duration; }

  // Stats
  const CombatStats &stats() const { return m_stats; }
  const CombatEncounter &currentEncounter() const { return m_currentEncounter; }

  /**
   * @brief Start tracking
   */
  void start();

  /**
   * @brief Stop tracking
   */
  void stop();

  /**
   * @brief Update the MumbleLink reference dynamically (Phase 7b-2b)
   */
  void setMumbleLink(MumbleLink *mumble);

  /**
   * @brief Reset current encounter
   */
  void resetEncounter();

  /**
   * @brief Manually add damage (for ArcDPS integration)
   */
  void addDamage(double amount, const QString &skillName = QString());

signals:
  void combatStateChanged(bool inCombat);
  void statsUpdated();
  void encounterStarted();
  void encounterEnded(const CombatEncounter &encounter);

private slots:
  void onMumbleUpdate();
  void onMapChanged(uint32_t mapId);
  void updateStats();

private:
  void enterCombat();
  void exitCombat();
  void detectCombatFromMumble();

  MumbleLink *m_mumble;
  QTimer *m_updateTimer;
  QElapsedTimer m_combatTimer;

  bool m_inCombat = false;
  CombatStats m_stats;
  CombatEncounter m_currentEncounter;
  QList<CombatEncounter> m_encounterHistory;

  // Position tracking for movement-based estimation
  float m_lastX = 0, m_lastY = 0, m_lastZ = 0;
  int m_stationaryFrames = 0;

  // Estimated damage (very rough from health changes)
  double m_estimatedDamage = 0;
};

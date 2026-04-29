#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSharedMemory>
#include <QTimer>


#include "ArcDPSModels.h"

namespace ArcDPS {

/**
 * @brief Real-time integration with ArcDPS via shared memory
 *
 * ArcDPS exposes combat data through shared memory when using certain plugins.
 * This provides real-time DPS data without parsing logs.
 *
 * NOTE: Requires ArcDPS extras plugin for full functionality.
 *
 * DO NOT ADD:
 * - Inline implementations (use ArcDPSRealTime.cpp)
 */
class RealTimeIntegration : public QObject {
  Q_OBJECT

public:
  explicit RealTimeIntegration(QObject *parent = nullptr);
  ~RealTimeIntegration();

  /**
   * @brief Check if ArcDPS is available
   */
  bool isAvailable() const { return m_available; }

  /**
   * @brief Start real-time monitoring
   */
  bool start();

  /**
   * @brief Stop monitoring
   */
  void stop();

  /**
   * @brief Get current encounter data
   */
  struct EncounterData {
    bool inCombat = false;
    uint64_t combatStartTime = 0;
    uint64_t duration = 0;

    QString targetName;
    float targetHealth = 1.0f;

    struct PlayerData {
      QString name;
      QString accountName;
      uint32_t profession;
      uint64_t damage;
      double dps;
      int hits;
      int crits;
      float critRate;
    };
    QList<PlayerData> players;

    // Self stats
    PlayerData self;
    uint64_t totalGroupDPS = 0;
  };

  const EncounterData &currentEncounter() const { return m_encounter; }

signals:
  void availabilityChanged(bool available);
  void combatStarted();
  void combatEnded();
  void dataUpdated(const EncounterData &data);

private slots:
  void pollData();

private:
  bool connectToArcDPS();
  void parseSharedData();

  QTimer *m_pollTimer;
  QSharedMemory *m_sharedMem;

  bool m_available = false;
  bool m_wasInCombat = false;
  EncounterData m_encounter;

  // Shared memory keys to try
  static constexpr const char *ARCDPS_SHMEM_KEY = "arcdps_combat_data";
  static constexpr const char *ARCDPS_EXTRAS_KEY = "arcdps_extras";
};

} // namespace ArcDPS

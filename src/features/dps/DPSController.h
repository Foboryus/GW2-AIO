#pragma once

#include <QObject>

#include "ArcDPSWatcher.h"
#include "CombatModels.h"
#include "CombatTracker.h"
#include "DPSWidget.h"
#include "core/MumbleLink.h"


/**
 * @brief Main controller for DPS tracking features
 *
 * Supports two data sources:
 * - MumbleOnly: Basic combat detection via Mumble Link API
 * - ArcDPS: Full DPS tracking via EVTC log parsing
 *
 * DO NOT ADD:
 * - Inline implementations (use DPSController.cpp)
 */
class DPSController : public QObject {
  Q_OBJECT

public:
  explicit DPSController(MumbleLink *mumble, QObject *parent = nullptr);
  ~DPSController();

  /**
   * @brief Data source for DPS tracking
   */
  enum class DataSource { MumbleOnly, ArcDPS };

  /**
   * @brief Start DPS tracking
   */
  void start();

  /**
   * @brief Stop DPS tracking
   */
  void stop();

  /**
   * @brief Update the MumbleLink reference dynamically (Phase 7b-2b)
   */
  void setMumbleLink(MumbleLink *mumble);

  /**
   * @brief Show/hide the DPS meter widget
   */
  void setMeterVisible(bool visible);
  bool isMeterVisible() const;

  /**
   * @brief Get the combat tracker for Mumble-based tracking
   */
  CombatTracker *tracker() { return m_tracker; }

  /**
   * @brief Get the ArcDPS log watcher
   */
  ArcDPS::LogWatcher *arcWatcher() { return m_arcWatcher; }

  /**
   * @brief Reset current encounter stats
   */
  void reset();

  /**
   * @brief Toggle between Mumble-only and ArcDPS modes
   */
  void setDataSource(DataSource source);
  DataSource dataSource() const { return m_dataSource; }

  /**
   * @brief Get recent parsed logs
   */
  const QList<ArcDPS::ParsedLog> &recentLogs() const;

signals:
  void meterVisibilityChanged(bool visible);
  void dataSourceChanged(DataSource source);
  void newLogParsed(const ArcDPS::ParsedLog &log);

private slots:
  void onArcLogParsed(const ArcDPS::ParsedLog &log);

private:
  MumbleLink *m_mumble;
  CombatTracker *m_tracker;
  DPSWidget *m_widget;
  ArcDPS::LogWatcher *m_arcWatcher;
  DataSource m_dataSource = DataSource::MumbleOnly;
};

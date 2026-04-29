/**
 * @file DPSController.cpp
 * @brief Main controller for DPS tracking features
 *
 * Supports two data sources:
 * - MumbleOnly: Basic combat detection via Mumble Link API
 * - ArcDPS: Full DPS tracking via EVTC log parsing
 *
 * DO NOT ADD:
 * - Log parsing (belongs in EVTCParser)
 * - Combat detection (belongs in CombatTracker)
 */

#include "DPSController.h"

#include <QDebug>

DPSController::DPSController(MumbleLink *mumble, QObject *parent)
    : QObject(parent), m_mumble(mumble),
      m_tracker(new CombatTracker(mumble, this)),
      m_widget(new DPSWidget(m_tracker)),
      m_arcWatcher(new ArcDPS::LogWatcher(this)) {
  // Position widget in top-right area by default
  m_widget->move(100, 100);

  // Connect ArcDPS signals
  connect(m_arcWatcher, &ArcDPS::LogWatcher::logParsed, this,
          &DPSController::onArcLogParsed);
}

DPSController::~DPSController() {
  stop();
  delete m_widget;
}

void DPSController::start() {
  m_tracker->start();

  // Also start ArcDPS watcher if using that source
  if (m_dataSource == DataSource::ArcDPS) {
    m_arcWatcher->start();
  }

  qInfo() << "DPS Controller started (source:"
          << (m_dataSource == DataSource::MumbleOnly ? "Mumble" : "ArcDPS")
          << ")";
}

void DPSController::stop() {
  m_tracker->stop();
  m_arcWatcher->stop();
  m_widget->hide();
}

void DPSController::setMumbleLink(MumbleLink *mumble) {
  m_mumble = mumble;
  m_tracker->setMumbleLink(mumble);
}

void DPSController::setMeterVisible(bool visible) {
  if (visible) {
    m_widget->show();
  } else {
    m_widget->hide();
  }
  emit meterVisibilityChanged(visible);
}

bool DPSController::isMeterVisible() const { return m_widget->isVisible(); }

void DPSController::reset() { m_tracker->resetEncounter(); }

void DPSController::setDataSource(DataSource source) {
  if (m_dataSource == source)
    return;

  m_dataSource = source;

  if (source == DataSource::ArcDPS) {
    m_arcWatcher->start();
    qInfo() << "DPS source: ArcDPS logs";
    qInfo() << "Log directory:" << ArcDPS::LogWatcher::defaultLogDirectory();
  } else {
    m_arcWatcher->stop();
    qInfo() << "DPS source: Mumble Link (limited accuracy)";
  }

  emit dataSourceChanged(source);
}

const QList<ArcDPS::ParsedLog> &DPSController::recentLogs() const {
  return m_arcWatcher->recentLogs();
}

void DPSController::onArcLogParsed(const ArcDPS::ParsedLog &log) {
  qInfo() << "New encounter:" << log.targetName << "DPS:" << log.dps
          << "Duration:" << log.duration / 1000 << "s";

  emit newLogParsed(log);
}

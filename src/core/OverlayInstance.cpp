/**
 * @file OverlayInstance.cpp
 * @brief Per-GW2-process overlay bundle implementation
 *
 * See OverlayInstance.h for class documentation.
 */

#include "OverlayInstance.h"

#include "MumbleLink.h"
#include "features/markers/MarkerController.h"
#include "features/markers/MarkerSettingsManager.h"

#include <QDebug>

// ============================================================================
// Constructor / Destructor
// ============================================================================

OverlayInstance::OverlayInstance(const QString &profileId,
                                 const QString &mumbleLinkName,
                                 const QString &markerStateDir,
                                 MarkerController *markerController,
                                 QObject *parent)
    : QObject(parent), m_profileId(profileId), m_mumbleLinkName(mumbleLinkName),
      m_markerController(markerController) {

  m_mumbleLink = new MumbleLink(m_mumbleLinkName, this);
  m_markerSettings = new MarkerSettingsManager(markerStateDir, this);

  qInfo() << "OverlayInstance: created for profile:" << m_profileId
          << "mumbleLink:" << m_mumbleLinkName;
}

OverlayInstance::~OverlayInstance() {
  if (m_running) {
    stop();
  }
  // m_mumbleLink and m_markerSettings are QObject children — auto-deleted.
  qInfo() << "OverlayInstance: destroyed for profile:" << m_profileId;
}

// ============================================================================
// Lifecycle
// ============================================================================

void OverlayInstance::start() {
  if (m_running) {
    qWarning() << "OverlayInstance::start() called while already running,"
               << "profile:" << m_profileId;
    return;
  }

  qInfo() << "OverlayInstance: starting — profile:" << m_profileId
          << "mumbleLink:" << m_mumbleLinkName;

  // Main process only needs connection/spawn monitoring — 10Hz is sufficient.
  // Child processes handle their own high-frequency polling independently.
  // (5 profiles × 10Hz = 50 timer events/sec vs previous 500/sec at 100Hz)
  m_mumbleLink->start(100);

  // Load per-profile marker preferences — needed for IPC settings push
  m_markerSettings->loadForProfile(m_profileId);

  m_running = true;
  emit started(m_profileId);

  qInfo() << "OverlayInstance: started — profile:" << m_profileId;
}

void OverlayInstance::stop() {
  if (!m_running) {
    return;
  }

  qInfo() << "OverlayInstance: stopping — profile:" << m_profileId;

  m_mumbleLink->stop();

  m_running = false;
  emit stopped(m_profileId);

  qInfo() << "OverlayInstance: stopped — profile:" << m_profileId;
}

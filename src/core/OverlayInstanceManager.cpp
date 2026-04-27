/**
 * @file OverlayInstanceManager.cpp
 * @brief Manages per-GW2-process overlay instances
 *
 * See OverlayInstanceManager.h for class documentation.
 */

#include "OverlayInstanceManager.h"

#include "LaunchManager.h"
#include "OverlayInstance.h"
#include "features/markers/MarkerController.h"

#include <QDebug>

// ============================================================================
// Constructor / Destructor
// ============================================================================

OverlayInstanceManager::OverlayInstanceManager(LaunchManager *launchManager,
                                               MarkerController *markerController,
                                               const QString &markerStateDir,
                                               QObject *parent)
    : QObject(parent), m_launchManager(launchManager),
      m_markerController(markerController), m_markerStateDir(markerStateDir) {

  // Self-contained wiring (Option A): connect to LaunchManager signals
  // internally. Main.cpp only needs to create the manager.

  // Creation: GW2 window confirmed → resolve link name → create overlay
  connect(m_launchManager, &LaunchManager::profileWindowConfirmed, this,
          &OverlayInstanceManager::onProfileWindowConfirmed);

  // Destruction: GW2 process exited → destroy overlay
  // NOTE: profileExited signal is added to LaunchManager in Phase 4.
  // This file is not compiled until Phase 5 (CMakeLists update).
  connect(m_launchManager, &LaunchManager::profileExited, this,
          [this](const QString &profileId) { destroyOverlay(profileId); });

  qInfo() << "OverlayInstanceManager: created — markerStateDir:"
          << m_markerStateDir;
}

OverlayInstanceManager::~OverlayInstanceManager() {
  destroyAll();
  qInfo() << "OverlayInstanceManager: destroyed";
}

// ============================================================================
// Instance lifecycle
// ============================================================================

OverlayInstance *
OverlayInstanceManager::createOverlay(const QString &profileId,
                                      const QString &mumbleLinkName) {
  // Check if overlay already exists for this profile
  if (m_instances.contains(profileId)) {
    auto *existing = m_instances.value(profileId);
    if (existing->mumbleLinkName() == mumbleLinkName) {
      // Same mumble link — true duplicate, return existing
      qInfo() << "[DEV] OverlayInstanceManager: overlay already exists with"
                 " same mumbleLink — returning existing, profile:" << profileId;
      return existing;
    }
    // Different mumble link name — destroy old SYNCHRONOUSLY and recreate.
    // Must NOT use destroyOverlay() which calls deleteLater() — the deferred
    // destructor would fire AFTER the new instance is created, uninstalling
    // hooks that the new OverlayWindow already installed.
    // (e.g. initial "MumbleLink" → runtime "GW2MumbleLink2")
    qInfo() << "[DEV] OverlayInstanceManager: mumbleLink changed from"
            << existing->mumbleLinkName() << "to" << mumbleLinkName
            << "— destroying old overlay SYNC for profile:" << profileId;
    auto *old = m_instances.take(profileId);
    old->stop();
    delete old;  // Synchronous — fully destroyed before new instance
    emit overlayDestroyed(profileId);
    qInfo() << "[DEV] OverlayInstanceManager: old overlay destroyed sync"
            << "— active instances:" << m_instances.count();
  }

  qInfo() << "OverlayInstanceManager: creating overlay — profile:" << profileId
          << "mumbleLink:" << mumbleLinkName;

  auto *instance = new OverlayInstance(profileId, mumbleLinkName,
                                       m_markerStateDir, m_markerController,
                                       this);
  m_instances.insert(profileId, instance);

  connect(instance, &OverlayInstance::focusChanged, this,
          [this, instance](bool focused) {
            if (focused) {
              qInfo() << "OverlayInstanceManager: instance focused, firing focusedMumbleLinkChanged — profile:"
                      << instance->profileId();
              emit focusedMumbleLinkChanged(instance->mumbleLink());
            }
          });

  // Give controllers the initial instance since m_isFocused starts as true
  emit focusedMumbleLinkChanged(instance->mumbleLink());

  instance->start();

  emit overlayCreated(profileId);

  qInfo() << "OverlayInstanceManager: overlay created — active instances:"
          << m_instances.count();

  return instance;
}

void OverlayInstanceManager::destroyOverlay(const QString &profileId) {
  auto *instance = m_instances.take(profileId);
  if (!instance) {
    qInfo() << "OverlayInstanceManager: no overlay to destroy for profile:"
            << profileId;
    return;
  }

  qInfo() << "OverlayInstanceManager: destroying overlay — profile:"
          << profileId;

  instance->stop();
  instance->deleteLater();

  emit overlayDestroyed(profileId);

  if (m_instances.isEmpty()) {
    qInfo() << "OverlayInstanceManager: last instance destroyed, firing focusedMumbleLinkChanged(nullptr)";
    emit focusedMumbleLinkChanged(nullptr);
  }

  qInfo() << "OverlayInstanceManager: overlay destroyed — active instances:"
          << m_instances.count();
}

void OverlayInstanceManager::destroyAll() {
  if (m_instances.isEmpty()) {
    return;
  }

  qInfo() << "OverlayInstanceManager: destroying all"
          << m_instances.count() << "overlay(s)";

  // Collect keys first — destroyOverlay modifies m_instances
  const QList<QString> profileIds = m_instances.keys();
  for (const QString &profileId : profileIds) {
    destroyOverlay(profileId);
  }
}

// ============================================================================
// Getters
// ============================================================================

OverlayInstance *
OverlayInstanceManager::instance(const QString &profileId) const {
  return m_instances.value(profileId, nullptr);
}

QList<OverlayInstance *> OverlayInstanceManager::instances() const {
  return m_instances.values();
}

// ============================================================================
// Private slots
// ============================================================================

void OverlayInstanceManager::onProfileWindowConfirmed(
    const QString &profileId) {
  // Resolve mumble link name from LaunchManager
  QString linkName = m_launchManager->mumbleLinkNameForProfile(profileId);
  if (linkName.isEmpty()) {
    linkName = QStringLiteral("MumbleLink");
  }

  qInfo() << "OverlayInstanceManager: profileWindowConfirmed — profile:"
          << profileId << "resolved linkName:" << linkName;

  createOverlay(profileId, linkName);
}

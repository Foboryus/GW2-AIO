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

#include <QCoreApplication>
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
              m_focusedMumbleLink = instance->mumbleLink();
              emit focusedMumbleLinkChanged(m_focusedMumbleLink);
            }
          });

  // Give controllers the initial instance since m_isFocused starts as true
  m_focusedMumbleLink = instance->mumbleLink();
  emit focusedMumbleLinkChanged(m_focusedMumbleLink);

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

  qInfo() << "[TEARDOWN] OverlayInstanceManager: BEGIN destroy — profile:"
          << profileId << "remaining:" << m_instances.count();

  // Step 1: Stop the MumbleLink timer — no more timer events
  qInfo() << "[TEARDOWN] Step 1: stopping instance...";
  instance->stop();
  qInfo() << "[TEARDOWN] Step 1: instance stopped";

  // Step 2: Disconnect ALL signals FROM the instance (as sender)
  qInfo() << "[TEARDOWN] Step 2: disconnecting instance signals...";
  instance->disconnect();
  qInfo() << "[TEARDOWN] Step 2: instance disconnected";

  // Step 3: Disconnect ALL signals FROM MumbleLink (as sender)
  if (instance->mumbleLink()) {
    qInfo() << "[TEARDOWN] Step 3: disconnecting MumbleLink signals...";
    instance->mumbleLink()->disconnect();
    qInfo() << "[TEARDOWN] Step 3: MumbleLink disconnected";
  } else {
    qInfo() << "[TEARDOWN] Step 3: no MumbleLink to disconnect";
  }

  // Step 4: Process pending events to flush any queued signals
  // that may have been dispatched before disconnect() was called.
  qInfo() << "[TEARDOWN] Step 4: flushing event queue...";
  QCoreApplication::processEvents();
  qInfo() << "[TEARDOWN] Step 4: event queue flushed";

  // CRITICAL: If the destroyed instance owned the currently-focused MumbleLink,
  // we MUST null-out the pointer BEFORE deleting. Otherwise RadialController,
  // DPSController, ModuleController hold a dangling pointer → crash.
  if (instance->mumbleLink() == m_focusedMumbleLink) {
    qInfo() << "[TEARDOWN] Focused instance being destroyed — emitting focusedMumbleLinkChanged(nullptr)";
    m_focusedMumbleLink = nullptr;
    emit focusedMumbleLinkChanged(nullptr);
  }

  // Step 5: Synchronous delete — safe because:
  // - stop() halted the timer (no new timer events)
  // - disconnect() severed all signal connections
  // - processEvents() flushed any pending queued signals
  // - focused pointer nulled out above if needed
  qInfo() << "[TEARDOWN] Step 5: deleting instance...";
  delete instance;
  qInfo() << "[TEARDOWN] Step 5: instance deleted";

  emit overlayDestroyed(profileId);

  // Note: focusedMumbleLinkChanged(nullptr) for "last instance" case is
  // already handled above (the last instance is always the focused one).

  qInfo() << "[TEARDOWN] OverlayInstanceManager: END destroy — profile:"
          << profileId << "active instances:" << m_instances.count();
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

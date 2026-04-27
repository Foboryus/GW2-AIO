#pragma once

/**
 * @file OverlayInstance.h
 * @brief Per-GW2-process management-only overlay bundle (Phase 11)
 *
 * Management-only bundle for one GW2 process:
 *   - MumbleLink (per-instance shared memory reader)
 *   - MarkerSettingsManager (per-profile marker preferences)
 *
 * Rendering components (D3D11OverlayWindow, MinimapRenderer, OverlayWindow)
 * are owned by child processes — NOT by this class.
 *
 * Ownership: OverlayInstance owns MumbleLink and MarkerSettingsManager.
 * It receives a shared MarkerController pointer (read-only after load).
 *
 * Lifecycle:
 *   constructor → creates MumbleLink + MarkerSettingsManager
 *   start()     → MumbleLink poll, load settings
 *   stop()      → stop polling (idempotent, safe to call twice)
 *   destructor  → calls stop() if needed, QObject children auto-deleted
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Rendering code (belongs in child processes)
 * - Global state (this is a per-instance class)
 * - Inline implementations beyond trivial getters (use OverlayInstance.cpp)
 */

class MarkerController;
class MarkerSettingsManager;
class MumbleLink;

#include <QObject>
#include <QString>



class OverlayInstance : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Construct an overlay bundle (does NOT start it)
   * @param profileId     UUID of the profile this overlay serves
   * @param mumbleLinkName Shared memory segment name (e.g. "MumbleLink",
   *                       "MumbleLink2")
   * @param markerStateDir Base directory for MarkerSettingsManager storage
   * @param markerController Shared marker data (not owned — read-only after
   *                         pack load)
   * @param parent         QObject parent for lifetime management
   */
  OverlayInstance(const QString &profileId, const QString &mumbleLinkName,
                  const QString &markerStateDir,
                  MarkerController *markerController,
                  QObject *parent = nullptr);
  ~OverlayInstance();

  // --- Lifecycle ---

  /** Start all overlay components. Safe to call only once. */
  void start();

  /** Stop all overlay components. Idempotent — safe to call multiple times. */
  void stop();

  // --- Getters ---

  bool isRunning() const { return m_running; }
  QString profileId() const { return m_profileId; }
  QString mumbleLinkName() const { return m_mumbleLinkName; }

  MumbleLink *mumbleLink() const { return m_mumbleLink; }
  MarkerSettingsManager *markerSettings() const { return m_markerSettings; }

signals:
  /** Emitted after start() completes successfully. */
  void started(const QString &profileId);

  /** Emitted after stop() completes. */
  void stopped(const QString &profileId);

  /** Emitted when window focus state changes (Phase 7b-2b). */
  void focusChanged(bool focused);


private:
  Q_DISABLE_COPY_MOVE(OverlayInstance)

  // Identity
  QString m_profileId;
  QString m_mumbleLinkName;

  // Owned components (QObject children — auto-deleted)
  MumbleLink *m_mumbleLink = nullptr;
  MarkerSettingsManager *m_markerSettings = nullptr;

  // Shared (not owned)
  MarkerController *m_markerController = nullptr;

  // State
  bool m_running = false;
};

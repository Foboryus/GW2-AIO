#pragma once

/**
 * @file OverlayInstanceManager.h
 * @brief Manages per-GW2-process overlay instances
 *
 * Self-contained manager (Option A): receives LaunchManager* in constructor
 * and internally wires to profileWindowConfirmed / profileExited signals.
 * Main.cpp only needs to create the manager — no external signal wiring.
 *
 * Lifecycle: destroy-then-recreate. When GW2 exits, the instance is fully
 * torn down. On re-launch, a fresh instance is created with the new PID
 * and MumbleLink segment name.
 *
 * Manager ↔ UI pattern: emits signals, never creates UI.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Inline implementations beyond trivial getters (use .cpp)
 */

#include <QHash>
#include <QObject>
#include <QString>

class LaunchManager;
class MarkerController;
class MumbleLink;
class OverlayInstance;

class OverlayInstanceManager : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Construct the manager and wire to LaunchManager signals
   * @param launchManager Provides profileWindowConfirmed / profileExited
   * @param markerController Shared marker data (passed to each instance)
   * @param markerStateDir Base directory for MarkerSettingsManager storage
   * @param parent QObject parent for lifetime management
   */
  OverlayInstanceManager(LaunchManager *launchManager,
                         MarkerController *markerController,
                         const QString &markerStateDir,
                         QObject *parent = nullptr);
  ~OverlayInstanceManager();

  // --- Instance lifecycle ---

  /**
   * @brief Create and start an overlay instance for a profile
   * @param profileId UUID of the profile
   * @param mumbleLinkName Shared memory segment name
   * @return Pointer to the created instance, or existing if duplicate
   */
  OverlayInstance *createOverlay(const QString &profileId,
                                 const QString &mumbleLinkName);

  /**
   * @brief Stop and destroy an overlay instance
   * @param profileId UUID of the profile to destroy
   */
  void destroyOverlay(const QString &profileId);

  /**
   * @brief Stop and destroy all overlay instances (AIO shutdown)
   */
  void destroyAll();

  // --- Getters ---

  OverlayInstance *instance(const QString &profileId) const;
  QList<OverlayInstance *> instances() const;
  int count() const { return m_instances.count(); }

signals:
  void overlayCreated(const QString &profileId);
  void overlayDestroyed(const QString &profileId);

  /** Emitted when an instance gains focus, or nullptr when last destroyed */
  void focusedMumbleLinkChanged(MumbleLink *mumble);

private slots:
  /**
   * @brief Handle LaunchManager::profileWindowConfirmed
   * Resolves mumble link name and creates overlay instance.
   */
  void onProfileWindowConfirmed(const QString &profileId);

private:
  Q_DISABLE_COPY_MOVE(OverlayInstanceManager)

  LaunchManager *m_launchManager = nullptr;
  MarkerController *m_markerController = nullptr;
  QString m_markerStateDir;

  QHash<QString, OverlayInstance *> m_instances;
};

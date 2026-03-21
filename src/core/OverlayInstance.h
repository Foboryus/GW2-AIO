#pragma once

/**
 * @file OverlayInstance.h
 * @brief Per-GW2-process overlay bundle
 *
 * Bundles the four components needed for one GW2 overlay:
 *   - MumbleLink (per-instance shared memory reader)
 *   - MarkerSettingsManager (per-profile marker preferences)
 *   - MinimapRenderer (per-instance 2D compass/map markers)
 *   - OverlayWindow (Qt transparent overlay — diamond icon, menu panel)
 *   - D3D11OverlayWindow (DirectComposition — 3D markers, trails, minimap)
 *
 * Ownership: OverlayInstance owns all four components. It receives a shared
 * MarkerController pointer (pack data + ImageCache, read-only after load).
 *
 * Lifecycle:
 *   constructor → heap-creates components (does NOT start them)
 *   start()     → MumbleLink poll, load settings, wire signals, start tracking
 *   stop()      → stop tracking, stop polling (idempotent, safe to call twice)
 *   destructor  → calls stop() if needed, deletes owned components
 *
 * Signal wiring uses 4-arg connect() with OverlayInstance as context receiver,
 * so Qt auto-disconnects all connections when the instance is destroyed.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Global state (this is a per-instance class)
 * - Inline implementations beyond trivial getters (use OverlayInstance.cpp)
 */

class D3D11OverlayWindow;
class ImageCache;
class MarkerController;
class MarkerSettingsManager;
class MinimapRenderer;
class MumbleLink;
class OverlayWindow;

#include <QObject>
#include <QString>

#include "features/markers/MarkerManager.h" // MarkerQueryContext

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
                  ImageCache *imageCache,
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
  OverlayWindow *overlayWindow() const { return m_overlayWindow; }
  D3D11OverlayWindow *d3dOverlay() const { return m_d3dOverlay; }

signals:
  /** Emitted after start() completes successfully. */
  void started(const QString &profileId);

  /** Emitted after stop() completes. */
  void stopped(const QString &profileId);

private:
  Q_DISABLE_COPY_MOVE(OverlayInstance)

  // Identity
  QString m_profileId;
  QString m_mumbleLinkName;

  // Owned components
  MumbleLink *m_mumbleLink = nullptr;
  MarkerSettingsManager *m_markerSettings = nullptr;
  MinimapRenderer *m_minimapRenderer = nullptr;
  OverlayWindow *m_overlayWindow = nullptr;
  D3D11OverlayWindow *m_d3dOverlay = nullptr;

  // Shared (not owned)
  MarkerController *m_markerController = nullptr;

  // State
  bool m_running = false;

  // Per-instance query context (Phase 7a)
  MarkerQueryContext m_queryContext;

  // Trail ref-counting: last map this instance acquired (Phase 7b)
  uint32_t m_lastMapId = 0;
};

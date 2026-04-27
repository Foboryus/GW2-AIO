#pragma once

/**
 * @file Child3DOverlay.h
 * @brief 3D overlay child process — renders 3D markers and trails
 *
 * This is the first (proof-of-concept) feature child. It runs as its own
 * process (GW2AIO-3d-<ProfileName>.exe) and renders:
 * - 3D marker billboards via MarkerPipeline
 * - 3D trail ribbons via TrailPipeline
 * - Distance labels via SpriteBatch
 *
 * It does NOT render:
 * - Minimap markers (separate minimap child, Phase 8)
 * - Big map markers (separate bigmap child, Phase 8)
 * - The Qt overlay HUD (diamond icon, menu panel — stays in grandfather)
 *
 * Architecture:
 * - Owns its own D3D11OverlayWindow, MarkerManager, MarkerSettingsManager
 * - Creates its own MarkerQueryContext for per-instance routing
 * - Reads MumbleLink directly (inherited from ChildProcess)
 * - Receives settings from grandfather via named pipe (push model)
 * - Loads marker packs on first map enter, then only current map's trails
 */

#include "ChildProcess.h"

class D3D11OverlayWindow;
class ImageCache;
class MarkerManager;
class MarkerSettingsManager;
struct MarkerQueryContext;

class Child3DOverlay : public ChildProcess {
  Q_OBJECT

public:
  explicit Child3DOverlay(const QString &profileId,
                          const QString &mumbleName,
                          qint64 gw2Pid,
                          const QString &pipeName,
                          const QString &profileName,
                          QObject *parent = nullptr);
  ~Child3DOverlay() override;

protected:
  // --- ChildProcess virtual overrides ---
  bool onInitialize() override;
  void onShutdown() override;
  void onMapEntered(uint32_t mapId) override;
  void onMapLeft() override;
  void onFocusChanged(bool focused) override;
  void onSettingsReceived(const QJsonObject &settings) override;
  void onReloadPacks() override;

private:
  // Owned components
  D3D11OverlayWindow *m_d3dOverlay = nullptr;
  MarkerManager *m_markerManager = nullptr;
  MarkerSettingsManager *m_markerSettings = nullptr;
  ImageCache *m_imageCache = nullptr;

  // Per-instance query context
  MarkerQueryContext *m_queryContext = nullptr;

  // State
  bool m_packsLoaded = false;
};

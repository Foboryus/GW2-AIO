#pragma once

/**
 * @file ChildOverlay.h
 * @brief Overlay HUD child process — Qt overlay menu, diamond, settings
 *
 * Owns: OverlayWindow (full mode — menu widget, zone editor),
 *       MarkerSettingsManager (for toggle display in overlay menu)
 *
 * Architecture: This child process provides the in-game UI overlay.
 * It creates a full OverlayWindow (not headless) with the diamond icon,
 * settings panel, and exclusion zone editor. When user changes settings
 * via the overlay menu, the changes are written to disk and a
 * SETTING_CHANGED message is sent via pipe to the grandfather, which
 * then propagates to sibling children (Phase 16).
 *
 * This child does NOT render markers/trails — those are handled by
 * GW2AIO-3d and GW2AIO-minimap children.
 */

#include "ChildProcess.h"

class MarkerController;
class MarkerManager;
class MarkerSettingsManager;
class ImageCache;
class OverlayWindow;

class ChildOverlay : public ChildProcess {
  Q_OBJECT

public:
  ChildOverlay(const QString &profileId,
               const QString &mumbleName,
               qint64 gw2Pid,
               const QString &pipeName,
               const QString &profileName,
               QObject *parent = nullptr);
  ~ChildOverlay() override;

protected:
  bool onInitialize() override;
  void onShutdown() override;
  void onMapEntered(uint32_t mapId) override;
  void onMapLeft() override;
  void onFocusChanged(bool focused) override;
  void onSettingsReceived(const QJsonObject &settings) override;

private:
  MarkerSettingsManager *m_markerSettings = nullptr;
  MarkerManager *m_markerManager = nullptr;
  ImageCache *m_imageCache = nullptr;
  MarkerController *m_markerController = nullptr;
  OverlayWindow *m_overlayWindow = nullptr;

  bool m_packsLoaded = false;
};

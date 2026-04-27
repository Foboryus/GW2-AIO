#pragma once

/**
 * @file ChildMinimap.h
 * @brief Minimap child process — renders minimap markers + trails
 *
 * Owns: OverlayWindow (transparent HWND tracker), MinimapRenderer (QPainter 2D),
 *       MarkerManager, MarkerSettingsManager, ImageCache
 *
 * Architecture: Replicates the minimap portion of the old OverlayInstance,
 * but in its own isolated child process. The OverlayWindow handles GW2 HWND
 * tracking, click-through, and z-order. MinimapRenderer is embedded as a
 * child widget that fills the overlay and paints markers onto the compass area.
 */

#include "ChildProcess.h"

class ImageCache;
class MarkerManager;
class MarkerSettingsManager;
class MinimapRenderer;
class OverlayWindow;
struct MarkerQueryContext;

class ChildMinimap : public ChildProcess {
  Q_OBJECT

public:
  ChildMinimap(const QString &profileId,
               const QString &mumbleName,
               qint64 gw2Pid,
               const QString &pipeName,
               const QString &profileName,
               QObject *parent = nullptr);
  ~ChildMinimap() override;

protected:
  bool onInitialize() override;
  void onShutdown() override;
  void onMapEntered(uint32_t mapId) override;
  void onMapLeft() override;
  void onFocusChanged(bool focused) override;
  void onSettingsReceived(const QJsonObject &settings) override;
  void onReloadPacks() override;

private:
  void syncMinimapSettings();

  MarkerSettingsManager *m_markerSettings = nullptr;
  ImageCache *m_imageCache = nullptr;
  MarkerManager *m_markerManager = nullptr;
  MinimapRenderer *m_minimapRenderer = nullptr;
  OverlayWindow *m_overlayWindow = nullptr;
  MarkerQueryContext *m_queryContext = nullptr;

  bool m_packsLoaded = false;
};

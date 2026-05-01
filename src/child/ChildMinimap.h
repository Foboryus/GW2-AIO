#pragma once

/**
 * @file ChildMinimap.h
 * @brief Minimap child process — renders minimap markers + trails
 *
 * Owns: MinimapRenderer (QPainter 2D → QImage), D3D11Context (offscreen),
 *       SharedTextureProducer, MarkerManager, MarkerSettingsManager, ImageCache
 *
 * Architecture: MinimapRenderer paints to a QImage via renderToImage().
 * The QImage is uploaded to a SharedTexture (D3D11) which the compositor
 * samples and composites into the overlay. No OverlayWindow needed.
 *
 * GW2 window dimensions are polled via FindWindow + GetClientRect
 * to size the QImage correctly for compass position calculation.
 */

#include "ChildProcess.h"

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11_1.h>
#include <wrl/client.h>

#include <QImage>

class D3D11Context;
class ImageCache;
class MarkerManager;
class MarkerSettingsManager;
class MinimapRenderer;
class SharedTextureProducer;
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
  void onRenderFrame();
  void pollGW2WindowSize();

  // --- GW2 window discovery ---
  HWND m_gw2Hwnd = nullptr;
  bool findGW2Window();

  // --- Rendering pipeline ---
  D3D11Context *m_d3dContext = nullptr;
  SharedTextureProducer *m_sharedTexture = nullptr;
  QImage m_renderImage;  // QImage target for MinimapRenderer
  int m_gw2Width = 0;
  int m_gw2Height = 0;

  // --- Feature components ---
  MarkerSettingsManager *m_markerSettings = nullptr;
  ImageCache *m_imageCache = nullptr;
  MarkerManager *m_markerManager = nullptr;
  MinimapRenderer *m_minimapRenderer = nullptr;
  MarkerQueryContext *m_queryContext = nullptr;

  bool m_packsLoaded = false;
};

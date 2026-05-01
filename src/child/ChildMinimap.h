#pragma once

/**
 * @file ChildMinimap.h
 * @brief Minimap child process — renders minimap markers + trails
 *
 * Owns: MinimapRenderer (QPainter 2D → QImage), SharedTextureProducer,
 *       MarkerManager, MarkerSettingsManager, ImageCache
 *
 * Architecture: MinimapRenderer paints to a QImage via renderToImage().
 * The QImage is uploaded to a SharedTexture (D3D11) which the compositor
 * samples and composites into the overlay. No OverlayWindow needed.
 *
 * GPU footprint: Uses a BARE D3D11 device (no blend states, no rasterizer,
 * no render targets) — only enough for SharedTextureProducer + UpdateSubresource.
 * This minimizes GPU memory to avoid D3D11CreateDevice E_OUTOFMEMORY when
 * running 5 profiles with multiple children each.
 */

#include "ChildProcess.h"

// clang-format off
#include <windows.h>
#include <wrl/client.h>
// clang-format on

#include <d3d11_1.h>

#include <QImage>

class ImageCache;
class MarkerManager;
class MarkerSettingsManager;
class MinimapRenderer;
class SharedTextureProducer;
struct MarkerQueryContext;

using Microsoft::WRL::ComPtr;

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

  // --- Bare D3D11 device (no D3D11Context — saves GPU memory) ---
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_deviceContext;

  // --- Rendering pipeline ---
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

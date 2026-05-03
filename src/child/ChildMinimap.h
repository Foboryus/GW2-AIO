#pragma once

/**
 * @file ChildMinimap.h
 * @brief Minimap child process — renders minimap markers + trails
 *
 * Owns: MinimapRenderer (QPainter 2D → QImage), TrailPipeline (GPU minimap/
 *       bigmap trails), SharedTextureProducer, MarkerManager,
 *       MarkerSettingsManager, ImageCache
 *
 * Architecture (Phase 5.9.1):
 * 1. MinimapRenderer paints 2D markers/dots to a QImage via renderToImage()
 * 2. QImage is uploaded to an intermediate D3D11 render target
 * 3. TrailPipeline renders GPU minimap/bigmap trails on top of the markers
 * 4. Intermediate RT is copied to SharedTexture for compositor sampling
 *
 * This child owns ALL minimap/bigmap rendering — both QPainter markers
 * and GPU trails. The 3D child (Child3DOverlay) only handles 3D world
 * rendering. This ensures minimap continues working when 3D is toggled off.
 *
 * GPU footprint: Uses D3D11Context::initializeOffscreen() (device + blend
 * states + rasterizer, no swap chain). Heavier than the old bare device
 * but necessary for TrailPipeline shader rendering.
 */

#include "ChildProcess.h"

// clang-format off
#include <windows.h>
#include <wrl/client.h>
// clang-format on

#include <d3d11_1.h>
#include <QImage>

class D3D11Context;
class ImageCache;
class MarkerManager;
class MarkerSettingsManager;
class MinimapRenderer;
class SharedTextureProducer;
class TrailPipeline;
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
  bool ensureD3D11();  ///< Lazy device init — called on first map entry or focus gain
  void teardownD3D11();  ///< Full teardown — destroys ALL GPU resources (shutdown only)
  void teardownSharedResources();  ///< Light teardown — shared texture + intermediate RT (unfocus)

  // --- D3D11 context (offscreen — no window, no swap chain) ---
  D3D11Context *m_d3dContext = nullptr;

  // --- Intermediate render target (QPainter upload + GPU trail composite) ---
  ComPtr<ID3D11Texture2D> m_intermediateRT;
  ComPtr<ID3D11RenderTargetView> m_intermediateRTV;

  // --- Rendering pipeline ---
  SharedTextureProducer *m_sharedTexture = nullptr;
  TrailPipeline *m_trailPipeline = nullptr;
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
  bool m_d3dInitialized = false;  ///< True after ensureD3D11() succeeds

  // --- Loading screen detection (Phase 5.8 — same pattern as Child3DOverlay) ---
  bool m_contentVisible = true;
  uint32_t m_lastUiTick = 0;
  qint64 m_lastTickChangeMs = 0;
  static constexpr qint64 kStallMs = 100;  // 100ms stall → loading screen (instant)
};

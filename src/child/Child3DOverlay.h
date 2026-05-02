#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>

/**
 * @file Child3DOverlay.h
 * @brief 3D overlay child process — renders 3D markers and trails
 *
 * This child renders to a SharedTexture (offscreen) that the ChildCompositor
 * composites into its single overlay window. No window ownership here.
 *
 * Renders:
 * - 3D marker billboards via MarkerPipeline
 * - 3D trail ribbons via TrailPipeline
 * - Distance labels via SpriteBatch
 *
 * Architecture:
 * - Owns D3D11Context (offscreen, device-only) + SharedTextureProducer
 * - Creates its own MarkerQueryContext for per-instance routing
 * - Reads MumbleLink directly (inherited from ChildProcess)
 * - Receives settings from grandfather via named pipe (push model)
 * - Loads marker packs on first map enter, then only current map's trails
 *
 * DO NOT ADD:
 * - Window creation code (belongs in ChildCompositor)
 * - Z-order management (eliminated by compositor pattern)
 */

#include "ChildProcess.h"

class D3D11Context;
class ImageCache;
class MarkerManager;
class MarkerSettingsManager;
class MarkerPipeline;
class TrailPipeline;
class SpriteBatch;
class GlyphAtlas;
class SharedTextureProducer;
struct MarkerQueryContext;

struct ExclusionZoneData;

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

private slots:
  void onRenderFrame();

private:
  // --- Rendering ---
  void render();
  bool createExclusionBuffer();
  void updateAndBindExclusionZones();

  // --- Owned components ---
  D3D11Context *m_d3dContext = nullptr;
  SharedTextureProducer *m_sharedTexture = nullptr;
  MarkerManager *m_markerManager = nullptr;
  MarkerSettingsManager *m_markerSettings = nullptr;
  ImageCache *m_imageCache = nullptr;

  // Rendering pipelines
  MarkerPipeline *m_markerPipeline = nullptr;
  TrailPipeline *m_trailPipeline = nullptr;
  SpriteBatch *m_spriteBatch = nullptr;
  GlyphAtlas *m_glyphAtlas = nullptr;

  // Exclusion zone constant buffer (opaque pointer, managed in .cpp)
  struct ID3D11Buffer *m_exclusionCB = nullptr;

  // Per-instance query context
  MarkerQueryContext *m_queryContext = nullptr;

  // State
  bool m_packsLoaded = false;
  bool m_contentVisible = true;
  bool m_lastWroteContent = false;  ///< Tracks if we need to clear shared texture on hide
  bool m_renderingEnabled = true;

  // Loading screen detection
  uint32_t m_lastUiTick = 0;
  qint64 m_lastTickChangeMs = 0;

  // GW2 window tracking
  HWND m_gw2Hwnd = nullptr;
  static HWND findGW2WindowByPid(DWORD pid);

  // Lazy D3D11 init (B7 fix) — called on first map entry or focus gain
  bool ensureD3D11();
  void teardownD3D11();  ///< Inverse of ensureD3D11 — destroys GPU resources on unfocus
  bool m_d3dInitialized = false;

  // Intermediate render target (non-shared, for pipeline Draw calls)
  Microsoft::WRL::ComPtr<ID3D11Texture2D> m_intermediateRT;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_intermediateRTV;
};

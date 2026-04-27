#pragma once

/**
 * @brief 3D marker billboard rendering pipeline
 *
 * Renders TacO-format markers as screen-aligned textured quads (billboards)
 * using the view/projection matrix computed from MumbleLink camera data.
 *
 * Projection algorithm (from TacO source):
 *   1. Build LookAt matrix from camera position + direction (MumbleLink)
 *   2. Build Perspective matrix from FOV (MumbleLink identity JSON)
 *   3. For each visible marker:
 *      a. Transform world position to clip space
 *      b. Filter by distance (fadeNear/fadeFar)
 *      c. Project to screen coordinates
 *      d. Render as textured billboard quad
 *
 * Consumers:
 * - D3D11OverlayWindow: calls render() each frame
 *
 * Data sources:
 * - MumbleLink: camera position, direction, FOV, map ID, UI state
 * - MarkerManager: filtered markers for current map
 * - MarkerSettingsManager: pack/category enable state
 * - ImageCache: marker textures (QImage → D3D11 SRV)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11.h>
#include <wrl/client.h>

#include <DirectXMath.h>

#include <QHash>
#include <QVector>
#include <QVector3D>

#include <string>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

class D3D11Context;
class MumbleLink;
class MarkerManager;
class MarkerSettingsManager;
class ImageCache;
class SpriteBatch;
class GlyphAtlas;

struct Marker;
struct MarkerQueryContext;

class MarkerPipeline {
public:
  MarkerPipeline(D3D11Context *context, MumbleLink *mumble,
                 MarkerManager *manager, MarkerSettingsManager *settings,
                 ImageCache *imageCache);
  ~MarkerPipeline();

  // Non-copyable
  MarkerPipeline(const MarkerPipeline &) = delete;
  MarkerPipeline &operator=(const MarkerPipeline &) = delete;

  /**
   * @brief Initialize shaders, buffers, and pipeline state
   */
  bool initialize();

  /**
   * @brief Render all visible markers for the current frame
   *
   * Reads camera state from MumbleLink, queries MarkerManager for
   * visible markers on the current map, and submits billboarded quads.
   */
  void render();

  /**
   * Pre-load all marker textures for the current map.
   * MUST be called BEFORE D3D11Context::beginFrame() — textures created
   * during an active D3D11 frame silently fail to render.
   */
  void preloadTextures();

  // --- Settings ---

  void setShowMarkers(bool show) { m_showMarkers = show; }
  void setOpacity(float opacity) { m_opacity = opacity; }
  void setMaxRenderDistance(float dist) { m_maxRenderDistance = dist; }
  void setShowDistance(bool show) { m_showDistance = show; }
  void setMarkerScale(float scale) { m_markerScale = scale; }
  void setDistanceLabelOffset(float offset) { m_distanceLabelOffset = offset; }
  void setSpriteBatch(SpriteBatch *batch) { m_spriteBatch = batch; }
  void setGlyphAtlas(GlyphAtlas *atlas) { m_glyphAtlas = atlas; }

  /// Per-instance query context (Phase 7a) — null = use shared state
  void setQueryContext(const MarkerQueryContext *ctx) { m_queryCtx = ctx; }

  // Camera interpolation overrides (Optimization 3 — Blish HUD pattern)
  void setCameraOverride(const QVector3D &pos, const QVector3D &front) {
    m_camOverridePos = pos;
    m_camOverrideFront = front;
    m_useCameraOverride = true;
  }
  void setPlayerOverride(const QVector3D &pos) {
    m_playerOverridePos = pos;
    m_usePlayerOverride = true;
  }
  void clearOverrides() {
    m_useCameraOverride = false;
    m_usePlayerOverride = false;
  }

private:
  // --- Math helpers (TacO algorithm) ---

  DirectX::XMMATRIX buildViewMatrix() const;
  DirectX::XMMATRIX buildProjectionMatrix() const;

  float computeDistanceFade(float distance, float fadeNear,
                            float fadeFar) const;

  /**
   * @brief Compute exclusion zone alpha for a screen position
   *
   * Mirrors the HLSL computeExclusionAlpha() in marker.hlsl:
   * - Returns 0.0 inside any zone (fully hidden)
   * - Returns 0.0..1.0 in the fade fringe outside zones
   * - Returns 1.0 fully outside all zones
   *
   * @param sx Screen X position (pixels)
   * @param sy Screen Y position (pixels)
   * @param screenW Viewport width (pixels)
   * @param screenH Viewport height (pixels)
   */
  float computeExclusionAlpha(float sx, float sy, float screenW,
                              float screenH,
                              float labelHalfW = 0.0f,
                              float labelHalfH = 0.0f) const;

  // --- Texture management ---

  /**
   * @brief Get or create a D3D11 SRV from an ImageCache key
   */
  ID3D11ShaderResourceView *getOrCreateTexture(const QString &iconPath);

  /**
   * @brief Evict textures not used in the last ~300 frames (~5s at 60fps)
   */
  void sweepUnusedTextures();

  // --- Per-frame constant buffer data ---

  struct alignas(16) PerFrameCB {
    DirectX::XMFLOAT4X4 viewProjection;
    float screenWidth;
    float screenHeight;
    float fovScale; // 1/tan(fov/2) for TacO-parity billboard sizing
    float padding;
  };

  struct alignas(16) PerMarkerCB {
    DirectX::XMFLOAT3 worldPosition;
    float size;
    DirectX::XMFLOAT4 tintColor;
    float minSize;
    float maxSize;
    float padding[2];
  };

  // --- Members ---

  D3D11Context *m_context;
  MumbleLink *m_mumble;
  MarkerManager *m_markerManager;
  MarkerSettingsManager *m_markerSettings;
  ImageCache *m_imageCache;

  // Shader resources
  ComPtr<ID3D11VertexShader> m_vertexShader;
  ComPtr<ID3D11PixelShader> m_pixelShader;
  ComPtr<ID3D11InputLayout> m_inputLayout;

  // Constant buffers
  ComPtr<ID3D11Buffer> m_perFrameCB;
  ComPtr<ID3D11Buffer> m_perMarkerCB;

  // Quad geometry (single billboard quad, instanced per marker)
  ComPtr<ID3D11Buffer> m_quadVertexBuffer;
  ComPtr<ID3D11Buffer> m_quadIndexBuffer;

  // Texture cache: icon path → D3D11 SRV + LRU frame stamp
  // NOTE: Must use std::unordered_map, NOT QHash. QHash's value semantics
  // break ComPtr COM reference counting on MSVC, causing textures to
  // silently fail to render.
  struct CachedTexture {
    ComPtr<ID3D11ShaderResourceView> srv;
    uint64_t lastUsedFrame = 0;
  };
  std::unordered_map<std::string, CachedTexture> m_textureCache;
  uint64_t m_frameCount = 0;

  // Default texture (for markers without icons)
  ComPtr<ID3D11ShaderResourceView> m_defaultTexture;

  // Sampler
  ComPtr<ID3D11SamplerState> m_sampler;

  // Settings
  bool m_showMarkers = true;
  float m_opacity = 1.0f;
  float m_maxRenderDistance = 200.0f;  // Default 200m (user-configurable)
  bool m_showDistance = false;         // Distance label below markers
  float m_markerScale = 1.0f;          // Global marker size multiplier
  float m_distanceLabelOffset = 5.0f; // Distance label vertical offset (px)
  float m_cachedFovScale = 1.0f;       // Cached for CPU-side label offset
  bool m_initialized = false;

  // 2D text rendering (not owned — lifetime managed by D3D11OverlayWindow)
  SpriteBatch *m_spriteBatch = nullptr;
  GlyphAtlas *m_glyphAtlas = nullptr;

  // Camera interpolation overrides (Optimization 3)
  QVector3D m_camOverridePos;
  QVector3D m_camOverrideFront;
  QVector3D m_playerOverridePos;
  bool m_useCameraOverride = false;
  bool m_usePlayerOverride = false;

  // Per-instance query context (Phase 7a) — not owned
  const MarkerQueryContext *m_queryCtx = nullptr;
};

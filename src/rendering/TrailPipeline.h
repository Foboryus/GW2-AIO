#pragma once

/**
 * @brief 3D trail mesh rendering pipeline
 *
 * Renders TacO-format trails as textured 3D billboard strips.
 * Trail data comes from .trl binary files (array of float3 positions)
 * which are converted to camera-facing billboard strip meshes.
 *
 * Billboard strip generation:
 * For each pair of adjacent trail waypoints, create a quad that faces the
 * camera. The quad's width is the trail's configured width, and the texture
 * UV is mapped along the trail length for scrolling animation.
 *
 * Consumers:
 * - D3D11OverlayWindow: calls render() each frame
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11.h>
#include <wrl/client.h>

#include <DirectXMath.h>

#include <QElapsedTimer>
#include <QHash>
#include <QMatrix4x4>
#include <QRectF>
#include <QVector>

#include <string>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

class D3D11Context;
class MumbleLink;
class MarkerManager;
class MarkerSettingsManager;
class ImageCache;

struct Trail;
struct MarkerQueryContext;

class TrailPipeline {
public:
  TrailPipeline(D3D11Context *context, MumbleLink *mumble,
                MarkerManager *manager, MarkerSettingsManager *settings,
                ImageCache *imageCache);
  ~TrailPipeline();

  TrailPipeline(const TrailPipeline &) = delete;
  TrailPipeline &operator=(const TrailPipeline &) = delete;

  bool initialize();
  void render();
  void renderMinimap();
  void preloadTextures();

  void setShowTrails(bool show) { m_showTrails = show; }
  void setShowMinimap(bool show) { m_showMinimap = show; }
  void setShowBigMap(bool show) { m_showBigMap = show; }
  void setOpacity(float opacity) { m_opacity = opacity; }
  void setMaxRenderDistance(float dist) { m_maxRenderDistance = dist; }
  void setMinimapTrailWidth(float multiplier) { m_minimapTrailWidth = multiplier; }
  void setMinimapOpacity(float opacity) { m_minimapOpacity = opacity; }

  /// Per-instance query context (Phase 7a) — null = use shared state
  void setQueryContext(const MarkerQueryContext *ctx) { m_queryCtx = ctx; }

  // Camera interpolation overrides (Optimization 3 — Blish HUD pattern)
  // When set, render() uses these instead of raw MumbleLink camera data.
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
  // --- Trail vertex format ---
  struct TrailVertex {
    float x, y, z; // World-space position
    float u, v;    // Texture coordinate
    float alpha;   // Per-vertex alpha (fade at ends)
  };

  // --- Cached GPU mesh for a trail ---
  struct TrailMesh {
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11Buffer> indexBuffer;
    int vertexCount = 0;
    int indexCount = 0;
    bool dirty = true; // Needs rebuild
  };

  // --- Per-frame constant buffer (matches trail.hlsl cbuffer PerFrame) ---
  struct alignas(16) PerFrameCB {
    DirectX::XMFLOAT4X4 viewProjection;
    DirectX::XMFLOAT4X4 viewMatrix; // Camera-only matrix
    float time;
    float trailOpacity;
    float nearFadeStart;              // Near-player fade start (~3m)
    float nearFadeEnd;                // Near-player fade end (~4m)
    DirectX::XMFLOAT4 playerPosition; // Player world position

    // Per-trail far-distance fade (updated per draw call)
    float farFadeNear; // Pack fadeNear in meters (-1 = no fade)
    float farFadeFar;  // Pack fadeFar in meters (-1 = no fade)

    // Global max render distance (user setting)
    float maxRenderDist;
    float minimap2D; // 1.0 = minimap/bigmap, 0.0 = 3D world
  };

  // --- Methods ---

  void buildTrailMesh(const Trail &trail, TrailMesh &mesh);
  void buildMinimapTrailMesh(const Trail &trail, TrailMesh &mesh,
                             float worldWidth);
  QRectF computeMinimapRect() const;
  ID3D11ShaderResourceView *getOrCreateTexture(const QString &texturePath);

  /**
   * @brief Evict textures not used in the last ~300 frames (~5s at 60fps)
   */
  void sweepUnusedTextures();

  // --- Members ---
  D3D11Context *m_context;
  MumbleLink *m_mumble;
  MarkerManager *m_markerManager;
  MarkerSettingsManager *m_markerSettings;
  ImageCache *m_imageCache;

  // Shaders
  ComPtr<ID3D11VertexShader> m_vertexShader;
  ComPtr<ID3D11PixelShader> m_pixelShader;
  ComPtr<ID3D11InputLayout> m_inputLayout;

  // Constant buffer
  ComPtr<ID3D11Buffer> m_perFrameCB;

  // Trail mesh cache: trail key → GPU mesh
  // NOTE: Must use std::unordered_map — QHash breaks ComPtr refcounting
  std::unordered_map<std::string, TrailMesh> m_meshCache;

  // Texture cache (std::unordered_map — QHash breaks ComPtr refcounting)
  struct CachedTexture {
    ComPtr<ID3D11ShaderResourceView> srv;
    uint64_t lastUsedFrame = 0;
  };
  std::unordered_map<std::string, CachedTexture> m_textureCache;
  uint64_t m_frameCount = 0;
  ComPtr<ID3D11ShaderResourceView> m_defaultTexture;
  ComPtr<ID3D11SamplerState> m_sampler;
  ComPtr<ID3D11RasterizerState> m_rasterizerNoCull; // TacO: double-sided trails
  ComPtr<ID3D11RasterizerState> m_rasterizerScissor; // Minimap: scissor-clipped

  // Minimap mesh cache (separate from 3D — different width/projection)
  // NOTE: Must use std::unordered_map — QHash breaks ComPtr refcounting
  std::unordered_map<std::string, TrailMesh> m_minimapMeshCache;
  uint32_t m_lastMinimapMapId = 0; // Invalidate cache on map change
  float m_lastMinimapMapScale = -1.0f;    // Cache: rebuild on zoom change
  float m_lastMinimapWidthCached = -1.0f; // Cache: rebuild on width setting change
  bool m_lastBigMap = false;              // Cache: rebuild on minimap↔bigmap toggle

  // Animation time
  QElapsedTimer m_timer;

  // Settings
  bool m_showTrails = true;
  bool m_showMinimap = true;   // Minimap rendering toggle
  bool m_showBigMap = true;    // Big map (M key) rendering toggle
  float m_opacity = 1.0f;
  float m_maxRenderDistance = 200.0f; // Default 200m (user-configurable)
  float m_minimapTrailWidth = 1.0f;   // Trail width multiplier (1x–10x)
  float m_minimapOpacity = 1.0f;      // Minimap/bigmap trail opacity
  bool m_initialized = false;

  // Camera interpolation overrides (Optimization 3)
  QVector3D m_camOverridePos;
  QVector3D m_camOverrideFront;
  QVector3D m_playerOverridePos;
  bool m_useCameraOverride = false;
  bool m_usePlayerOverride = false;

  // Per-instance query context (Phase 7a) — not owned
  const MarkerQueryContext *m_queryCtx = nullptr;
};

#pragma once

/**
 * @brief Shared data structures for UI exclusion zones
 *
 * UI exclusion zones make trails/markers invisible (or faded) when they
 * overlap GW2 UI elements (minimap, skill bar, chat) or user-defined areas.
 *
 * The ExclusionCB is a D3D11 constant buffer at register(b2), shared by
 * both marker.hlsl and trail.hlsl pixel shaders. Each frame the overlay
 * computes active zones and uploads them.
 *
 * Zones use percentage coordinates [0..1] relative to the GW2 window,
 * so they scale automatically with resolution changes.
 *
 * DO NOT ADD:
 * - Rendering logic (belongs in pipelines/shaders)
 * - Persistence logic (belongs in MarkerSettingsManager)
 * - UI code (belongs in ExclusionZoneEditor)
 */

#include <DirectXMath.h>

#include <QString>

// Maximum exclusion zones supported (3 predefined + 5 custom)
static constexpr int kMaxExclusionZones = 8;

/**
 * @brief D3D11 constant buffer for exclusion zones (register b2)
 *
 * Bound to the pixel shader stage of both MarkerPipeline and TrailPipeline.
 * Updated once per frame by D3D11OverlayWindow before pipeline render calls.
 *
 * Layout matches HLSL cbuffer ExclusionZones:
 *   float4 Zones[8]    — (x, y, w, h) as percentage [0..1]
 *   float2 ScreenSize  — viewport pixels
 *   int    ZoneCount    — active zones (0..8)
 *   float  FadeEdge     — fade border width in percentage [0..0.05]
 */
struct alignas(16) ExclusionCB {
  DirectX::XMFLOAT4 zones[kMaxExclusionZones]; // (x, y, w, h) percentage
  float screenWidth;
  float screenHeight;
  int zoneCount;
  float fadeEdge; // 0.0 = hard cutoff, 0.02 = ~2% fade at edges
};

/**
 * @brief Data model for a single exclusion zone (used for persistence/UI)
 *
 * Stored in MarkerSettingsManager's _display.json (v3).
 * Coordinates are percentages [0..1] of the GW2 window.
 */
struct ExclusionZone {
  QString name; // User-friendly name (e.g., "DPS Meter", "Minimap")
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

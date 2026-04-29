/**
 * @file radial_common.hlsli
 * @brief Shared constants, CBs, samplers, and utilities for AIO radial shaders
 *
 * Clean-port of GW2Radial common.hlsli with AIO naming conventions.
 * All radial shaders include this file for consistent CB layout.
 *
 * MIT License (original: GW2Radial)
 */

#define PI 3.14159f
#define SQRT2 1.4142136f
#define ONE_OVER_SQRT2 0.707107f
#define RADIAL_MAX_ELEMENTS 20

#include "radial_noise.hlsl"

// ============================================================================
// Constant Buffers
// ============================================================================

// PS register(b0): Wheel-level state (shared across all draw calls per frame)
cbuffer RadialWheelCB : register(b0)
{
    float3 wipeMaskData;    // Wipe mask animation parameters
    float  wheelFadeIn;     // 0→1 wipe-in progress
    float  animationTimer;  // Monotonic time for noise animation
    float  centerScale;     // Dead zone radius (fraction of wheel)
    int    elementCount;    // Number of visible elements
    float  globalOpacity;   // Master opacity multiplier
    float4 hoverFadeIns_[RADIAL_MAX_ELEMENTS / 4]; // Packed per-element hover
    float  timeLeft;        // Delay indicator countdown [0,1]
    bool   showIcon;        // Show icon in delay indicator
};

// Helper: unpack per-element hover fade from packed float4 array
float GetHoverFadeIn(int i)
{
    return hoverFadeIns_[i >> 2][i & 3];
}

// PS register(b1): Per-element state (updated per element draw call)
cbuffer RadialElementCB : register(b1)
{
    float4 adjustedColor;       // Tint color for element icon
    float  elementHoverFadeIn;  // 0→1 hover animation for this element
    bool   premultiplyAlpha;    // True for premultiplied alpha icons
};

// ============================================================================
// Samplers & Textures
// ============================================================================

SamplerState MainSampler : register(s0);
SamplerState SecondarySampler : register(s1);

Texture2D<float4> BackgroundTexture : register(t0);
Texture2D<float4> WipeMaskTexture : register(t1);
Texture2D<float4> IconTexture : register(t1);

// ============================================================================
// Shared Structures
// ============================================================================

struct RadialPSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// ============================================================================
// Utility Functions
// ============================================================================

// Smooth random 2D displacement for background noise animation
float2 smoothRandom(float2 uv, float4 scales, float4 timeScales)
{
    float r1 = sin(scales.x * uv.x + animationTimer * timeScales.x)
             + sin(scales.y * uv.y + animationTimer * timeScales.y);
    float r2 = sin(scales.z * uv.x + animationTimer * timeScales.z)
             + sin(scales.w * uv.y + animationTimer * timeScales.w);
    return float2(r1, r2);
}

// 2D rotation matrix applied to UV coordinates
float2 rotateUV(float2 uv, float angle)
{
    float2x2 mat = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
    return mul(uv, mat);
}

// Remap value from [bounds.x, bounds.y] to [0, 1] (clamped)
float rescale(float value, float2 bounds)
{
    return saturate((value - bounds.x) / (bounds.y - bounds.x));
}

// Sample element icon with optional premultiplied alpha and tint
float4 sampleElementIcon(float2 uv, Texture2D tex, SamplerState samp, out float shadow)
{
    shadow = 0;
    float4 color = tex.Sample(samp, uv);
    if (premultiplyAlpha)
        color.rgb *= color.a;
    color *= adjustedColor;
    return color;
}

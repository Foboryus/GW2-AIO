/**
 * @file trail.hlsl
 * @brief Trail mesh vertex/pixel shader for 3D trail rendering
 *
 * Renders trails as textured 3D world-space ribbon strips with:
 * - UV scroll animation along trail length
 * - Smooth distance-based transparency near player (never fully invisible)
 * - Far-distance fade using per-trail fadeNear/fadeFar (TacO-compatible)
 * - Global max render distance cutoff (user-configurable)
 * - UI exclusion zones: trails fade out behind minimap, skill bar, etc.
 *
 * UV convention (TacO-compatible):
 * - U = across trail width (0=left, 1=right)
 * - V = along trail length (scrolling direction)
 *
 * Usage:
 * - VS: "VSMain", target "vs_5_0"
 * - PS: "PSMain", target "ps_5_0"
 */

// ============================================================================
// Constant Buffers
// ============================================================================

cbuffer PerFrame : register(b0) {
    row_major float4x4 ViewProjection;
    row_major float4x4 ViewMatrix;   // Camera-only matrix (for camera-space Z)
    float    Time;           // Elapsed seconds for UV scrolling
    float    TrailOpacity;   // Global trail opacity multiplier
    float    NearFadeStart;  // Near-player fade start (meters, ~3m)
    float    NearFadeEnd;    // Near-player fade end (meters, ~4m)
    float4   PlayerPosition; // Player world position

    // Per-trail far-distance fade (TacO-compatible)
    // When both are negative (pack default -1), no far fade is applied
    float    FarFadeNear;    // Far distance where fade starts (meters)
    float    FarFadeFar;     // Far distance where fully invisible (meters)

    // Global max render distance (user slider, meters)
    float    MaxRenderDist;

    float    minimap2D;      // 1.0 = minimap/bigmap mode (skip 3D fades), 0.0 = 3D world
};

// UI exclusion zones — shared between marker and trail shaders
// Zones are percentage-based [0..1] relative to the viewport
cbuffer ExclusionZones : register(b2) {
    float4 ExZones[8];       // (x, y, w, h) as percentage of viewport
    float2 ExScreenSize;     // Viewport size in pixels
    int    ExZoneCount;      // Number of active zones (0..8)
    float  ExFadeEdge;       // Fade border width in percentage [0..0.05]
};

// ============================================================================
// Vertex Input/Output
// ============================================================================

struct VSInput {
    float3 Position : POSITION; // World-space vertex position
    float2 TexCoord : TEXCOORD; // UV: U across width, V along trail length
    float  Alpha    : COLOR;    // Per-vertex alpha (for fade-in/out at ends)
};

struct PSInput {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float  Alpha    : COLOR0;
    float3 WorldPos : TEXCOORD1; // World position for distance fade
};

// ============================================================================
// Textures
// ============================================================================

Texture2D    TrailTexture  : register(t0);
SamplerState LinearSampler : register(s0);

// ============================================================================
// Vertex Shader
// ============================================================================

PSInput VSMain(VSInput input) {
    PSInput output;

    // Transform world-space trail vertex to clip space
    output.Position = mul(float4(input.Position, 1.0), ViewProjection);

    // UV scrolling: shift V coordinate over time for animated trail direction
    // U stays fixed (0=left edge, 1=right edge of trail strip)
    output.TexCoord = float2(input.TexCoord.x, input.TexCoord.y + Time);

    // Pass through per-vertex alpha and world position
    output.Alpha = input.Alpha;
    output.WorldPos = input.Position;

    return output;
}

// ============================================================================
// UI Exclusion Zone Helper
// ============================================================================

// Returns alpha multiplier: 0.0 inside zone, 1.0 outside, gradual fade at outer edges
float computeExclusionAlpha(float2 svPos) {
    if (ExZoneCount <= 0) return 1.0;

    // Normalize screen position to [0..1]
    float2 uv = svPos / ExScreenSize;

    float minAlpha = 1.0;

    for (int i = 0; i < ExZoneCount; i++) {
        float4 zone = ExZones[i];
        float2 zoneMin = zone.xy;
        float2 zoneMax = zone.xy + zone.zw;

        // Check if pixel is inside this zone (hard kill)
        if (uv.x >= zoneMin.x && uv.x <= zoneMax.x &&
            uv.y >= zoneMin.y && uv.y <= zoneMax.y) {
            return 0.0;
        }

        // Check if pixel is in the fade fringe OUTSIDE the zone
        if (ExFadeEdge > 0.0) {
            float2 outerMin = zoneMin - float2(ExFadeEdge, ExFadeEdge);
            float2 outerMax = zoneMax + float2(ExFadeEdge, ExFadeEdge);

            if (uv.x >= outerMin.x && uv.x <= outerMax.x &&
                uv.y >= outerMin.y && uv.y <= outerMax.y) {
                // Distance from pixel to nearest zone edge (outside)
                float dx = max(zoneMin.x - uv.x, uv.x - zoneMax.x);
                float dy = max(zoneMin.y - uv.y, uv.y - zoneMax.y);
                float edgeDist = max(max(dx, 0.0), max(dy, 0.0));

                // Fade: 0.0 at zone edge -> 1.0 at ExFadeEdge distance outside
                minAlpha = min(minAlpha, saturate(edgeDist / ExFadeEdge));
            }
        }
    }
    return minAlpha;
}

// ============================================================================
// Pixel Shader
// ============================================================================

float4 PSMain(PSInput input) : SV_TARGET {
    float4 texColor = TrailTexture.Sample(LinearSampler, input.TexCoord);

    // Initialize all fade factors to 1.0 (no fade)
    float nearFade = 1.0;
    float farFade = 1.0;
    float maxFade = 1.0;
    float exclusionAlpha = 1.0;

    // 3D world mode: apply distance fades and exclusion zones
    // Minimap/bigmap mode: skip all of these (flat 2D view, no depth)
    if (minimap2D < 0.5) {
        float dist = distance(input.WorldPos, PlayerPosition.xyz);

        // --- Near-player fade ---
        // Trails become partially transparent near player but NEVER fully invisible
        // Minimum alpha floor: trails are always at least 15% visible up close
        nearFade = saturate((dist - NearFadeStart) / max(NearFadeEnd - NearFadeStart, 0.01));
        nearFade = max(nearFade, 0.15);

        // --- Far-distance fade (TacO-compatible) ---
        if (FarFadeNear >= 0.0 && FarFadeFar > FarFadeNear) {
            farFade = saturate(1.0 - (dist - FarFadeNear) / (FarFadeFar - FarFadeNear));
        }

        // --- Global max render distance with full-range opacity gradient ---
        if (MaxRenderDist > 0.0) {
            float t = saturate(dist / MaxRenderDist);
            maxFade = 1.0 - sqrt(t);
        }

        // --- UI exclusion zones ---
        exclusionAlpha = computeExclusionAlpha(input.Position.xy);
    }

    // Combine all alpha factors
    float finalAlpha = texColor.a * input.Alpha * TrailOpacity * nearFade * farFade * maxFade * exclusionAlpha;

    float4 result = float4(texColor.rgb, finalAlpha);

    // Premultiply alpha for DWM composition
    result.rgb *= result.a;

    return result;
}

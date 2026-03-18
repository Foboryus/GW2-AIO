/**
 * @file marker.hlsl
 * @brief Billboard marker vertex/pixel shader for 3D marker rendering
 *
 * Renders markers as screen-aligned textured quads (billboards).
 * The vertex shader transforms world-space marker positions to screen-space
 * using the same view/projection matrix as TacO:
 *   1. World → Camera space (LookAt matrix from MumbleLink camera)
 *   2. Camera → Clip space (Perspective from MumbleLink FOV)
 *   3. Clip → NDC (perspective divide done by GPU)
 *
 * The pixel shader applies texture + tint color + alpha for distance fade.
 * UI exclusion zones hide markers behind GW2 UI elements.
 *
 * Usage:
 * - VS: "VSMain", target "vs_5_0"
 * - PS: "PSMain", target "ps_5_0"
 */

// ============================================================================
// Constant Buffer — updated per frame
// ============================================================================

cbuffer PerFrame : register(b0) {
    row_major float4x4 ViewProjection; // row_major: matches DirectXMath storage
    float2 ScreenSize;       // Viewport width, height in pixels
    float  FovScale;          // 1/tan(fov/2) for TacO-parity sizing
    float  _padding0;
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
// Constant Buffer — updated per marker
// ============================================================================

cbuffer PerMarker : register(b1) {
    float3 WorldPosition;    // Marker center in world space (from MumbleLink)
    float  Size;             // Marker size in world units
    float4 TintColor;        // RGBA tint (includes alpha for distance fade)
    float  MinSize;          // Minimum pixel size (clamp)
    float  MaxSize;          // Maximum pixel size (clamp)
    float2 _padding1;
};

// ============================================================================
// Vertex Input/Output
// ============================================================================

struct VSInput {
    float2 Position : POSITION; // Quad corner: (-0.5,-0.5) to (0.5,0.5)
    float2 TexCoord : TEXCOORD;
};

struct PSInput {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

// ============================================================================
// Textures
// ============================================================================

Texture2D    MarkerTexture : register(t0);
SamplerState LinearSampler : register(s0);

// ============================================================================
// Vertex Shader — Billboard
// ============================================================================

PSInput VSMain(VSInput input) {
    PSInput output;

    // Transform world position to clip space
    float4 clipPos = mul(float4(WorldPosition, 1.0), ViewProjection);

    // Skip markers behind camera
    if (clipPos.w <= 0.0) {
        output.Position = float4(0, 0, -1, 1); // Off-screen
        output.TexCoord = input.TexCoord;
        output.Color = float4(0, 0, 0, 0);
        return output;
    }

    // Perspective divide to get NDC
    float2 ndc = clipPos.xy / clipPos.w;

    // Calculate projected size in pixels
    // Reference: TacO uses a fixed "reference width" projected through perspective
    float projectedSize = (Size / clipPos.w) * ScreenSize.y * 0.5 * FovScale;

    // Clamp to min/max pixel size
    projectedSize = clamp(projectedSize, MinSize, MaxSize);

    // Billboard offset in pixels, then convert to NDC
    float2 offsetPixels = input.Position * projectedSize;
    float2 offsetNDC = offsetPixels / (ScreenSize * 0.5);

    // Final position in clip space
    output.Position = float4((ndc + offsetNDC) * clipPos.w, clipPos.z, clipPos.w);
    output.TexCoord = input.TexCoord;
    output.Color = TintColor;

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
    float4 texColor = MarkerTexture.Sample(LinearSampler, input.TexCoord);

    // UI exclusion zone fade
    float exclusionAlpha = computeExclusionAlpha(input.Position.xy);

    // Apply tint, alpha, and exclusion
    float4 result = texColor * input.Color;
    result.a *= exclusionAlpha;

    // Premultiply alpha for DWM composition
    result.rgb *= result.a;

    return result;
}

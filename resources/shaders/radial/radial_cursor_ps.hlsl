/**
 * @file radial_cursor_ps.hlsl
 * @brief Pixel shader for radial cursor glow indicator
 *
 * Renders a soft gold glow centered on the mouse position.
 * Uses noise-based distortion for organic-looking shimmer.
 *
 * Clean-port of GW2Radial Cursor.hlsl.
 */

#include "radial_common.hlsli"

float4 RadialCursorPS(RadialPSInput input) : SV_Target
{
    float2 centeredUV = 2 * (input.uv - 0.5f);
    float2 polar = float2(length(centeredUV), atan2(centeredUV.y, centeredUV.x));
    polar.y = 2 * (polar.y / PI + 1);

    // Noise-based shimmer
    float noiseVal = psrnoise(
        polar * float2(1.0f, 0.5f) - float2(0.5f, 0.05f) * animationTimer,
        float2(100, 2),
        wipeMaskData.z / (2 * PI)
    );

    // Gold cursor color (AIO brand-adjacent)
    float4 color = float4(194.0f/255.0f, 189.0f/255.0f, 149.0f/255.0f, 1.0f);
    color *= pow(1.0f - smoothstep(0.0f, 1.0f, polar.x), 4.0f);
    color *= 1 - lerp(0.1f, 1.0f, smoothstep(0.2f, 0.6f, polar.x)) * noiseVal;

    return float4(color.rgb * globalOpacity, 0.0f);
}

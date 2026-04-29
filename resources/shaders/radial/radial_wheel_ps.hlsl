/**
 * @file radial_wheel_ps.hlsl
 * @brief Pixel shader for radial wheel background ring
 *
 * Renders the textured background ring with:
 * - Polar-coordinate section division
 * - Noise-animated background texture
 * - Wipe-in fade animation
 * - Per-section hover brightening
 * - Edge/center masks for circular falloff
 * - Border glow between sections
 *
 * Clean-port of GW2Radial Wheel.hlsl.
 */

#include "radial_common.hlsli"

float GetWipeValue(in float2 uv, in float progress)
{
    return saturate(progress + 1.0f - length(uv - 0.5f) * 2);
}

float4 RadialWheelPS(RadialPSInput input) : SV_Target0
{
    float currentWheelFadeIn = GetWipeValue(input.uv, wheelFadeIn);

    // Mirror and scale UV to polar coordinate space
    float2 coords = -3 * (input.uv - 0.5f);
    // Compute polar coordinates: (radius, angle) with angle in [0, 2*PI)
    float2 coordsPolar = float2(length(coords), atan2(coords.y, coords.x) + PI);
    // Compensate for theta=0 direction
    coordsPolar.y += 0.5f * PI;

    // EARLY DISCARD: Skip expensive noise computation for pixels outside the
    // visible wheel ring. The ring is visible between centerScale and ~1.0 radius.
    // EdgeMask fades to 0 at radius >= 1.0 (smoothstep 0.5→1.0 on coordsPolar.x).
    // CenterMask fades to 0 below centerScale.
    // Adding small margins for the border glow effect.
    if (coordsPolar.x > 1.05f || coordsPolar.x < (centerScale - 0.05f))
    {
        return float4(0, 0, 0, 0);
    }

    // Angular span per element (e.g., 4 elements = 90 degrees each)
    float singleElementAngle = 2.0f * PI / float(elementCount);
    // Determine which element this pixel belongs to
    int localElementId = (int)round(coordsPolar.y / singleElementAngle);
    if (localElementId >= elementCount)
        localElementId -= elementCount;
    float hoverFade = GetHoverFadeIn(localElementId);
    bool isHovered = hoverFade > 0.0f;

    hoverFade = min(hoverFade, wheelFadeIn);

    // Percentage along the element's angular span (0 = one edge, 1 = other)
    float localCoordPct = fmod(coordsPolar.y + 0.5f * singleElementAngle, singleElementAngle) / singleElementAngle;

    // Noise-animated background sampling
    float2 noiseRandom = float2(
        srnoise(3 * input.uv * cos(0.1f * animationTimer) + animationTimer * 0.37f),
        srnoise(5 * input.uv * sin(0.13f * animationTimer) + animationTimer * 0.48f)
    );
    float4 color = BackgroundTexture.Sample(MainSampler, input.uv);
    color.a = 1.0f;
    color.rgb *= 2 * lerp(0.9f, 1.3f, saturate((4 + noiseRandom.x + noiseRandom.y) / 8));

    // Luma for desaturation effects
    float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));

    // Edge mask: fade out at circle periphery
    float edgeMask = lerp(1.0f, 0.0f, smoothstep(0.5f, 1.0f, coordsPolar.x * (1 - luma * 0.2f)));
    // Center mask: fade out in dead zone
    float centerMask = lerp(0.0f, 1.0f, smoothstep(centerScale - 0.01f, centerScale + 0.01f, coordsPolar.x * (1 - luma * 0.2f)));

    // Brighten hovered sections (only outside center zone)
    color.rgb *= lerp(1.0f, lerp(1.0f, 1.5f, hoverFade), centerMask);

    // Border glow between sections
    float borderMask = 1.0f;
    if (hoverFade < 1)
    {
        float minThickness = 0.004f / (0.001f + coordsPolar.x);
        float maxThickness = 0.006f / (0.001f + coordsPolar.x);

        if (elementCount > 1)
        {
            borderMask *= lerp(2.0f, 1.0f, smoothstep(minThickness, maxThickness, localCoordPct));
            borderMask *= lerp(1.0f, 2.0f, smoothstep(1 - maxThickness, 1 - minThickness, localCoordPct));
            borderMask = lerp(1.0f, borderMask, centerMask);
        }
    }

    // Graceful border fade during hover transition
    if (isHovered && hoverFade < 1)
        borderMask = lerp(borderMask, 1.0f, hoverFade);

    // Inner region flair
    borderMask *= 2.0f - smoothstep(centerScale + 0.01f, centerScale + 0.1f, coordsPolar.x);

    // Center hover (dead zone action)
    float centerHoverFade = GetHoverFadeIn(elementCount);
    if (centerHoverFade > 0.0f)
    {
        color.rgb *= lerp(lerp(1.0f, 1.5f, centerHoverFade), 1.0f, centerMask);
        centerMask = lerp(centerMask, 1 - luma * 0.2f, centerHoverFade);
    }

    // Combine all masks
    return color
        * saturate(edgeMask * centerMask)
        * clamp(borderMask, 1.0f, 2.0f)
        * clamp(luma, 0.8f, 1.2f)
        * currentWheelFadeIn
        * float4(1, 1, 1, 1.2f)
        * globalOpacity;
}

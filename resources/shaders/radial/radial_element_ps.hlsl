/**
 * @file radial_element_ps.hlsl
 * @brief Pixel shader for radial wheel element icons
 *
 * Renders each element's icon with luma-based desaturation when not hovered,
 * transitioning to full color on hover. Fades with wheel animation.
 *
 * Clean-port of GW2Radial WheelElement.hlsl.
 */

#include "radial_common.hlsli"

float4 RadialElementPS(RadialPSInput input) : SV_Target
{
    float shadow;
    float4 color = sampleElementIcon(input.uv, IconTexture, MainSampler, shadow);

    // Luma-based desaturation for unfocused elements
    const float3 lumaDot = float3(0.2126, 0.7152, 0.0722);
    float luma = dot(color.rgb, lumaDot);
    float3 fadedColor = lerp(color.rgb, luma, 0.4f);
    float3 finalColor = lerp(fadedColor, color.rgb, elementHoverFadeIn);

    return float4(finalColor.rgb, color.a) * wheelFadeIn * globalOpacity;
}

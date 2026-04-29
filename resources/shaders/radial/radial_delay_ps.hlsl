/**
 * @file radial_delay_ps.hlsl
 * @brief Pixel shader for radial delay indicator (pie-chart countdown)
 *
 * Renders a countdown timer as a pie-chart overlay with the selected
 * element icon centered. Used for queued mount selection.
 *
 * Clean-port of GW2Radial DelayIndicator.hlsl.
 * NOTE: This shader is compiled in Phase 1B but used in Phase 3.
 */

#include "radial_common.hlsli"

float4 RadialDelayPS(RadialPSInput input) : SV_Target
{
    float2 centeredUV = 2 * (input.uv - 0.5f);
    float2 polar = float2(length(centeredUV), atan2(centeredUV.y, centeredUV.x));
    polar.y = fmod(10 - (polar.y / PI + 0.5f) * 0.5f, 1.0f); // [0, 1]

    // Noise-animated background
    float2 noiseRandom = float2(
        srnoise(3 * input.uv * cos(0.5f * animationTimer) + animationTimer * 0.43f),
        srnoise(3 * input.uv * sin(0.79f * animationTimer) + animationTimer * 0.22f)
    );
    float4 baseColor = BackgroundTexture.Sample(MainSampler, input.uv);

    float4 color = saturate(baseColor * float2(2, 1).xxxy) * wheelFadeIn;
    color.rgb *= lerp(0.9f, 1.3f, saturate((4 + noiseRandom.x + noiseRandom.y) / 8));
    color.rgb *= 1.25f - (1 - smoothstep(0.60f, 0.80f, polar.x));

    // Circular falloff
    color.rgb *= 1.0f - smoothstep(0.80f, 0.90f, polar.x);
    color.a *= 1.0f - smoothstep(0.90f, 1.00f, polar.x);

    // Pie-chart countdown
    color *= 1.25f - smoothstep(timeLeft - 0.01f, timeLeft + 0.01f, polar.y);
    color.rgb *= smoothstep(0.0f, 0.02f, abs(timeLeft - polar.y));

    // Optional icon overlay
    if (showIcon)
    {
        float iconShadow;
        float4 iconColor = sampleElementIcon(centeredUV * 0.85f + 0.5f, IconTexture, SecondarySampler, iconShadow);

        color.rgb *= 1 - max(iconShadow, iconColor.a);
        color.rgb += iconColor.rgb * wheelFadeIn;
    }

    return color;
}

/**
 * @file sprite.hlsl
 * @brief 2D sprite rendering shader for UI elements
 *
 * Used by SpriteBatch for rendering:
 * - Overlay menu elements (text, icons, backgrounds)
 * - Minimap markers (2D positioned)
 * - Any screen-space 2D content
 *
 * Vertices are in screen pixels, transformed to NDC by the vertex shader.
 *
 * Usage:
 * - VS: "VSMain", target "vs_5_0"
 * - PS: "PSMain", target "ps_5_0"
 */

// ============================================================================
// Constant Buffer
// ============================================================================

cbuffer PerFrame : register(b0) {
    float2 ScreenSize;    // Viewport width, height in pixels
    float2 _padding0;
};

// ============================================================================
// Vertex Input/Output
// ============================================================================

struct VSInput {
    float2 Position : POSITION; // Screen-space pixel coordinates
    float2 TexCoord : TEXCOORD;
    float4 Color    : COLOR;
};

struct PSInput {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

// ============================================================================
// Textures
// ============================================================================

Texture2D    SpriteTexture : register(t0);
SamplerState LinearSampler : register(s0);

// ============================================================================
// Vertex Shader — Screen-Space
// ============================================================================

PSInput VSMain(VSInput input) {
    PSInput output;

    // Convert pixel coordinates to NDC: [0, width] → [-1, 1]
    float2 ndc;
    ndc.x = (input.Position.x / ScreenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (input.Position.y / ScreenSize.y) * 2.0; // Flip Y

    output.Position = float4(ndc, 0.0, 1.0);
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}

// ============================================================================
// Pixel Shader
// ============================================================================

float4 PSMain(PSInput input) : SV_TARGET {
    float4 texColor = SpriteTexture.Sample(LinearSampler, input.TexCoord);
    float4 result = texColor * input.Color;

    // Premultiply alpha for DWM composition
    result.rgb *= result.a;

    return result;
}

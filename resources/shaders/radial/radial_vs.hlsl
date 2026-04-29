/**
 * @file radial_vs.hlsl
 * @brief Vertex shader for radial menu quads
 *
 * Generates a screen-space quad from SV_VertexID (4 vertices, no vertex buffer).
 * Supports sprite positioning, scaling, and tilt matrix for parallax effect.
 *
 * Clean-port of GW2Radial ScreenQuad.hlsl.
 */

cbuffer RadialQuadCB : register(b0)
{
    float4   spriteDimensions; // xy = NDC center, zw = NDC half-size
    float4x4 tiltMatrix;      // Perspective tilt for parallax (identity = flat)
    float    spriteZ;         // Depth value (0 = front)
};

struct RadialVSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

RadialVSOutput RadialVS(in uint vertexId : SV_VertexID)
{
    RadialVSOutput output = (RadialVSOutput)0;

    // Generate UV from vertex index: 0=(0,0), 1=(1,0), 2=(0,1), 3=(1,1)
    float2 uv = float2(vertexId & 1, vertexId >> 1);

    // Map UV to NDC quad centered at spriteDimensions.xy
    float2 dims = (uv * 2 - 1) * spriteDimensions.zw;

    output.uv = uv;
    output.position = mul(
        float4(dims + spriteDimensions.xy * 2 - 1, spriteZ, 1.0f),
        tiltMatrix
    );
    output.position.z += saturate(0.5f - spriteZ);
    output.position.y *= -1;

    return output;
}

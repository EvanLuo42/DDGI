Texture2D gColorIn;
Texture2D gIrradianceAtlas;

SamplerState gSampler
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

cbuffer DDGIConstants
{
    float3 gOrigin;
    float3 gSpacing;
    uint3  gProbeCounts;
    uint   gTileResIrradiance;
    float  gGIIntensity;
};

StructuredBuffer<float3> gProbePositions;

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD;
};

VSOut vsMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2 - 1, 0, 1);
    return o;
}

float4 psMain(VSOut i) : SV_Target
{
    float3 base = gColorIn.Sample(gSampler, i.uv).rgb;
    float3 gi   = gIrradianceAtlas.Sample(gSampler, i.uv).rgb;

    return float4(base + gi * gGIIntensity, 1);
}

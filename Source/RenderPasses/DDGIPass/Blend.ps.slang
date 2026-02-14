#include "Utils/Math/MathConstants.slangh"

import Scene.Scene;
import Rendering.Lights.LightHelpers;

Texture2D gDepthIn;
Texture2D gNormalIn;
Texture2D gAlbedoIn;
Texture2D gEmissiveIn;
Texture2D gIrradianceAtlas;

SamplerState gSampler;

cbuffer DDGIConstants
{
    float3 gOrigin;
    float3 gSpacing;
    uint3  gProbeCounts;
    uint   gTileResIrradiance;
    float  gGIIntensity;
};

cbuffer PerFrameCB
{
    float4x4 gInvViewProj;
    float3   gCameraPos;
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
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0, 1);
    return o;
}

float3 reconstructWorldPos(float2 uv, float depth)
{
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    ndc.y = -ndc.y;
    float4 world = mul(gInvViewProj, ndc);
    return world.xyz / world.w;
}

float2 octEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0)
    {
        float2 sign_xy = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * sign_xy;
    }
    return n.xy * 0.5 + 0.5;
}

uint2 probeGridToAtlas(uint3 gridIdx)
{
    return uint2(gridIdx.x, gridIdx.y * gProbeCounts.z + gridIdx.z);
}

float3 sampleProbeIrradiance(uint3 probeGridIdx, float3 direction)
{
    uint2 atlasIdx = probeGridToAtlas(probeGridIdx);

    float2 octUV = octEncode(direction);

    uint2 irrDim;
    irrDim.x = gProbeCounts.x * gTileResIrradiance;
    irrDim.y = gProbeCounts.y * gProbeCounts.z * gTileResIrradiance;

    float2 tileOrigin = float2(atlasIdx * gTileResIrradiance) + 0.5;
    float2 tileInterior = float(gTileResIrradiance) - 1.0;
    float2 texelCoord = tileOrigin + octUV * tileInterior;
    float2 atlasUV = texelCoord / float2(irrDim);

    return gIrradianceAtlas.SampleLevel(gSampler, atlasUV, 0).rgb;
}

float3 computeDirectLighting(float3 worldPos, float3 normal, float3 albedo)
{
    float3 result = float3(0.0);
    uint lightCount = gScene.getLightCount();

    for (uint i = 0; i < lightCount; i++)
    {
        AnalyticLightSample ls;
        if (evalLightApproximate(worldPos, gScene.getLight(i), ls))
        {
            float NdotL = max(0.0, dot(normal, ls.dir));
            result += albedo * (1.0 / M_PI) * ls.Li * NdotL;
        }
    }

    return result;
}

float4 psMain(VSOut i) : SV_Target
{
    float depth = gDepthIn.Load(int3(i.pos.xy, 0)).r;

    if (depth >= 1.0)
        return float4(0.0, 0.0, 0.0, 1.0);

    float3 worldPos = reconstructWorldPos(i.uv, depth);
    float3 normal   = normalize(gNormalIn.Load(int3(i.pos.xy, 0)).rgb);
    float3 albedo   = gAlbedoIn.Load(int3(i.pos.xy, 0)).rgb;
    float3 emissive = gEmissiveIn.Load(int3(i.pos.xy, 0)).rgb;

    float3 directColor = emissive + computeDirectLighting(worldPos, normal, albedo);

    float3 gridCoordF = (worldPos - gOrigin) / gSpacing;

    int3   baseIdx = int3(floor(gridCoordF));
    float3 frac3   = gridCoordF - float3(baseIdx);

    int3 maxIdx = int3(gProbeCounts) - int3(1);

    float3 irradiance = float3(0.0);
    float  totalWeight = 0.0;

    for (int dz = 0; dz <= 1; ++dz)
    for (int dy = 0; dy <= 1; ++dy)
    for (int dx = 0; dx <= 1; ++dx)
    {
        int3 idx = clamp(baseIdx + int3(dx, dy, dz), int3(0), maxIdx);
        uint3 probeIdx = uint3(idx);

        float3 w3 = float3(dx, dy, dz) * frac3 + float3(1 - dx, 1 - dy, 1 - dz) * (1.0 - frac3);
        float trilinearWeight = w3.x * w3.y * w3.z;

        float3 probePos = gProbePositions[idx.x + idx.y * int(gProbeCounts.x) + idx.z * int(gProbeCounts.x * gProbeCounts.y)];
        float3 dirToProbe = normalize(probePos - worldPos);
        float backfaceWeight = max(0.0001, dot(normal, dirToProbe));

        float weight = trilinearWeight * backfaceWeight;

        irradiance += sampleProbeIrradiance(probeIdx, normal) * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0)
        irradiance /= totalWeight;

    float3 gi = irradiance * albedo * (1.0 / M_PI);

    return float4(directColor + gi * gGIIntensity, 1.0);
}

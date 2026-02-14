#include "Utils/Math/MathConstants.slangh"

cbuffer DDGIConstants
{
    uint gTileResRadiance;
    uint gTileResIrradiance;
    uint3 gProbeCounts;
};

Texture2D<float4> gRadianceAtlas;
RWTexture2D<float4> gIrradianceAtlas;

float3 sampleFibonacciSphere(uint index, uint count)
{
    const float PHI = 1.6180339887498948482;
    float i = float(index);
    float n = float(count);
    float cosTheta = 1.0 - (2.0 * i + 1.0) / n;
    float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    float phi = 2.0 * M_PI * frac(i / PHI);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Octahedral decode: [0,1]^2 -> unit direction
float3 octDecode(float2 e)
{
    e = e * 2.0 - 1.0;
    float3 v = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
    {
        float2 sign_xy = float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
        v.xy = (1.0 - abs(v.yx)) * sign_xy;
    }
    return normalize(v);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 irrDim;
    gIrradianceAtlas.GetDimensions(irrDim.x, irrDim.y);
    if (any(id.xy >= irrDim))
        return;

    uint2 probeCoord2D = uint2(id.x / gTileResIrradiance, id.y / gTileResIrradiance);
    uint2 localPix = uint2(id.x % gTileResIrradiance, id.y % gTileResIrradiance);

    float2 octUV = (float2(localPix) + 0.5) / float(gTileResIrradiance);
    float3 texelDir = octDecode(octUV);

    uint raysPerProbe = gTileResRadiance * gTileResRadiance;
    float3 irradiance = float3(0.0);
    float  totalWeight = 0.0;

    for (uint rayIdx = 0; rayIdx < raysPerProbe; ++rayIdx)
    {
        float3 rayDir = sampleFibonacciSphere(rayIdx, raysPerProbe);

        float cosTheta = dot(texelDir, rayDir);
        if (cosTheta <= 0.0)
            continue;

        uint2 rayPixel = uint2(rayIdx % gTileResRadiance, rayIdx / gTileResRadiance);
        uint2 radCoord = probeCoord2D * gTileResRadiance + rayPixel;

        float3 rad = gRadianceAtlas[radCoord].rgb;

        irradiance += rad * cosTheta;
        totalWeight += cosTheta;
    }

    if (totalWeight > 0.0)
        irradiance /= totalWeight;

    gIrradianceAtlas[id.xy] = float4(irradiance, 1.0);
}

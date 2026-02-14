import Scene.Raytracing;
import Scene.RaytracingInline;
import Rendering.Lights.LightHelpers;
import Utils.Sampling.SampleGenerator;

#include "Utils/Math/MathConstants.slangh"

cbuffer DDGIConstants
{
    uint gTileResTrace;
    uint gTileResRadiance;
    uint3 gProbeCounts;
};

cbuffer PerFrameCB
{
    uint gFrameCount;
};

Texture2D<float4> gHitPosAtlas;
Texture2D<float4> gHitNormalAtlas;
Texture2D<float4> gHitAlbedoAtlas;
RWTexture2D<float4> gRadianceAtlas;

uint2 radianceToTraceCoord(uint2 radTid)
{
    uint2 probeCoord2D = uint2(radTid.x / gTileResRadiance, radTid.y / gTileResRadiance);
    uint2 localPix = uint2(radTid.x % gTileResRadiance, radTid.y % gTileResRadiance);
    return probeCoord2D * gTileResTrace + localPix;
}

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID)
{
    uint2 dim;
    gRadianceAtlas.GetDimensions(dim.x, dim.y);
    if (any(tid >= dim))
        return;

    uint2 pixel = uint2(tid.x % gTileResRadiance, tid.y % gTileResRadiance);

    uint2 traceCoord = radianceToTraceCoord(tid);
    float4 hitPos = gHitPosAtlas[traceCoord];

    uint traceRayIndex = pixel.y * gTileResRadiance + pixel.x;
    uint raysPerProbe = gTileResRadiance * gTileResRadiance;

    float3 rayDir;
    {
        const float PHI = 1.6180339887498948482;
        float fi = float(traceRayIndex);
        float fn = float(raysPerProbe);
        float cosTheta = 1.0 - (2.0 * fi + 1.0) / fn;
        float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
        float phi = 2.0 * M_PI * frac(fi / PHI);
        rayDir = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    }

    if (hitPos.w == 0)
    {
        float3 skyColor = gScene.envMap.eval(rayDir);
        gRadianceAtlas[tid] = float4(skyColor, 1.0f);
        return;
    }

    float3 normal = normalize(gHitNormalAtlas[traceCoord].xyz);
    float3 albedo = gHitAlbedoAtlas[traceCoord].rgb;
    float3 radiance = 0;

    SampleGenerator sg = SampleGenerator(tid, gFrameCount);

    for (uint i = 0; i < gScene.getLightCount(); ++i)
    {
        LightData light = gScene.lights[i];

        AnalyticLightSample ls;
        sampleLight(hitPos.xyz, light, sg, ls);

        float NdotL = saturate(dot(normal, ls.dir));
        if (NdotL <= 0)
            continue;

        const Ray shadowRay = Ray(hitPos.xyz + normal * 0.001, ls.dir, 0.0, ls.distance);
        bool visible = traceSceneVisibilityRay<0>(shadowRay, RAY_FLAG_NONE, 0xff);

        if (visible)
        {
            float3 f = albedo * (1.0 / M_PI);
            radiance += f * ls.Li * NdotL;
        }
    }

    gRadianceAtlas[tid] = float4(radiance, 1);
}

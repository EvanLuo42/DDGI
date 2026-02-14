#include "Utils/Math/MathConstants.slangh"

import Scene.Scene;
import Scene.Raytracing;

cbuffer DDGIConstants
{
    uint3 gProbeCounts;
    uint gTileRes;
    float gMaxRayDistance;
};

StructuredBuffer<float3> gProbePositions;

RWTexture2D<float4> gHitPosAtlas;
RWTexture2D<float4> gHitNormalAtlas;
RWTexture2D<float4> gHitAlbedoAtlas;

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

uint2 computeAtlasCoord(uint probeIndex, uint2 pixel, uint tileRes)
{
    uint3 probeCoord;
    probeCoord.x = probeIndex % gProbeCounts.x;
    probeCoord.y = (probeIndex / gProbeCounts.x) % gProbeCounts.y;
    probeCoord.z = probeIndex / (gProbeCounts.x * gProbeCounts.y);

    uint atlasX = probeCoord.x * tileRes + pixel.x;
    uint atlasY = (probeCoord.y + probeCoord.z * gProbeCounts.y) * tileRes + pixel.y;

    return uint2(atlasX, atlasY);
}

struct Payload
{
    bool hit;
    float3 worldPos;
    float3 normal;
    float3 albedo;
};

[shader("raygeneration")]
void rayGen()
{
    uint3 launchID = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    uint2 pixel = launchID.xy;
    uint probeIndex = launchID.z;

    uint tileRes = gTileRes;
    uint raysPerProbe = tileRes * tileRes;

    uint rayIndex = pixel.y * tileRes + pixel.x;

    if (rayIndex >= raysPerProbe)
        return;

    float3 probePos = gProbePositions[probeIndex];

    float3 dir = sampleFibonacciSphere(rayIndex, raysPerProbe);

    RayDesc ray;
    ray.Origin = probePos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = gMaxRayDistance;

    Payload payload;
    payload.hit = false;

    TraceRay(gScene.rtAccel, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    uint2 atlasCoord = computeAtlasCoord(probeIndex, pixel, tileRes);

    if (payload.hit)
    {
        gHitPosAtlas[atlasCoord] = float4(payload.worldPos, 1);
        gHitNormalAtlas[atlasCoord] = float4(payload.normal, 0);
        gHitAlbedoAtlas[atlasCoord] = float4(payload.albedo, 1);
    }
    else
    {
        gHitPosAtlas[atlasCoord] = 0;
        gHitNormalAtlas[atlasCoord] = 0;
        gHitAlbedoAtlas[atlasCoord] = 0;
    }
}

[shader("closesthit")]
void closestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);

    payload.worldPos = v.posW;
    payload.normal = normalize(v.normalW);

    const uint materialID = gScene.getMaterialID(instanceID);
    ShadingData sd = gScene.materials.prepareShadingData(v, materialID, -WorldRayDirection());

    let lod = ExplicitLodTextureSampler(0.f);
    let mi = gScene.materials.getMaterialInstance(sd, lod);
    let bsdfProperties = mi.getProperties(sd);
    payload.albedo = bsdfProperties.diffuseReflectionAlbedo;

    payload.hit = true;
}

[shader("miss")]
void miss(inout Payload payload)
{
    payload.hit = false;
}

[shader("anyhit")]
void anyHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    AcceptHitAndEndSearch();
}

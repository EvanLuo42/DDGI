// Minimal Falcor-compatible RT program that actually traces.
// Works for triangle meshes and links cleanly.

RaytracingAccelerationStructure gScene;

cbuffer DDGIConstants
{
    uint3 gProbeCounts;
    uint  gTileRes;
    float gMaxRayDistance;
};

StructuredBuffer<float3> gProbePositions;

RWTexture2D<float4> gHitPosAtlas;
RWTexture2D<float4> gHitNormalAtlas;
RWTexture2D<float4> gHitAlbedoAtlas;

// Payload must be used by TraceRay for many pipelines to link correctly.
struct Payload
{
    float3 color;
    float  t;
    float3 normal;
    uint   hit;    // 0 miss, 1 hit
};

[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();   // (x,y,probeIndex)
    uint2 pix = launchIndex.xy;
    uint  probeIndex = launchIndex.z;

    Payload p;
    p.color = float3(0, 0, 0);
    p.t = 0;
    p.normal = float3(0, 1, 0);
    p.hit = 0;

    float3 origin = gProbePositions[probeIndex];

    // Placeholder direction (not octahedral yet): forward-ish
    float2 uv = (float2(pix) + 0.5) / float(gTileRes);
    float2 xy = uv * 2.0 - 1.0;
    float3 dir = normalize(float3(xy, 1.0));

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.01;
    ray.TMax = gMaxRayDistance;

    // Flags 0; hitGroup 0; miss 0
    TraceRay(gScene, 0, 0xFF, 0, 1, 0, ray, p);

    // Write out
    if (p.hit != 0)
    {
        float3 pos = origin + dir * p.t;
        gHitPosAtlas[pix]    = float4(pos, 1);
        gHitNormalAtlas[pix] = float4(normalize(p.normal), 1);
        gHitAlbedoAtlas[pix] = float4(p.color, 1);
    }
    else
    {
        // Miss values
        gHitPosAtlas[pix]    = float4(0,0,0,1);
        gHitNormalAtlas[pix] = float4(0,1,0,1);
        gHitAlbedoAtlas[pix] = float4(0,0,0,1);
    }
}

[shader("miss")]
void miss(inout Payload p)
{
    p.hit = 0;
    p.color = float3(0, 0, 0);
    p.normal = float3(0, 1, 0);
    p.t = 0;
}

// Triangle closest hit signature should include BuiltInTriangleIntersectionAttributes.
[shader("closesthit")]
void closestHit(inout Payload p, BuiltInTriangleIntersectionAttributes attr)
{
    p.hit = 1;
    p.t = RayTCurrent();

    // Placeholder normal: face normal approximation is not available from attr alone.
    // Use ray direction to make something stable:
    p.normal = -WorldRayDirection();

    // Placeholder albedo:
    p.color = float3(1, 1, 1);
}

[shader("anyhit")]
void anyHit(inout Payload p, BuiltInTriangleIntersectionAttributes attr)
{
    // For placeholder, do nothing (no alpha test).
}

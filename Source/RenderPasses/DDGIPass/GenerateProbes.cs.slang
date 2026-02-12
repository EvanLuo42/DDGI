cbuffer DDGIConstants
{
    float3 gOrigin;
    float3 gSpacing;
    uint3 gProbeCounts;
};

RWStructuredBuffer<float3> gProbePositions;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gProbeCounts.x || id.y >= gProbeCounts.y || id.z >= gProbeCounts.z)
        return;

    uint index = id.x + id.y * gProbeCounts.x + id.z * gProbeCounts.x * gProbeCounts.y;

    gProbePositions[index] = gOrigin + (float3(id) + 0.5f) * gSpacing;
}

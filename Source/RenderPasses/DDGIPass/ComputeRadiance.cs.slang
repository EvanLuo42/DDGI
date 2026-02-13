cbuffer DDGIConstants
{
    uint gTileResTrace;
    uint gTileResRadiance;
    uint3 gProbeCounts;
};

Texture2D<float4> gHitAlbedoAtlas;
RWTexture2D<float4> gRadianceAtlas;

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 albedo = gHitAlbedoAtlas[id.xy];
    gRadianceAtlas[id.xy] = albedo;
}

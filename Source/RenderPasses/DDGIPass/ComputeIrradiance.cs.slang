cbuffer DDGIConstants
{
    uint gTileResRadiance;
    uint gTileResIrradiance;
    uint3 gProbeCounts;
};

Texture2D<float4> gRadianceAtlas;
RWTexture2D<float4> gIrradianceAtlas;

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 rad = gRadianceAtlas[id.xy];
    gIrradianceAtlas[id.xy] = rad * 0.5;
}

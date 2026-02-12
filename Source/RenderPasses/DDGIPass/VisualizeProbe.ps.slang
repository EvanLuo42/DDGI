struct VSIn
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct VSOut
{
    float4 posH : SV_Position;
    float3 normalW : NORMAL;
    float3 posW : POSITION;
    nointerpolation uint instanceID : INSTANCE_ID;
};

cbuffer PerFrameCB
{
    float4x4 gViewProj;
    float3 gCameraPos;
    float gProbeRadius;
    float3 gProbeColor;
};

StructuredBuffer<float3> gProbePositions;

VSOut vsMain(VSIn vIn, uint instanceID : SV_InstanceID)
{
    VSOut vOut;

    float3 probePos = gProbePositions[instanceID];
    float3 worldPos = vIn.position * gProbeRadius + probePos;

    vOut.posH = mul(gViewProj, float4(worldPos, 1.0f));
    vOut.posW = worldPos;
    vOut.normalW = vIn.normal;
    vOut.instanceID = instanceID;

    return vOut;
}

float4 psMain(VSOut pIn) : SV_Target
{
    float3 normal = normalize(pIn.normalW);

    float3 lightDir = normalize(float3(0.5f, 1.0f, 0.3f));

    float NdotL = max(dot(normal, lightDir), 0.0f);
    float ambient = 0.3f;
    float diffuse = 0.7f * NdotL;

    float3 finalColor = gProbeColor * (ambient + diffuse);

    return float4(finalColor, 1.0f);
}

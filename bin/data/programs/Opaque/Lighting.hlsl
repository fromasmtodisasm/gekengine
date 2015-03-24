#include "..\gekengine.h"
#include "..\gektypes.h"
#include "..\gekutility.h"
#include "..\geklights.h"

groupshared uint    g_nTileMinDepth;
groupshared uint    g_nTileMaxDepth;
groupshared uint    g_nNumTileLights;
groupshared uint    g_aTileLightList[gs_nMaxLights];
groupshared float4  g_aTileFrustum[6];
RWBuffer<uint>      g_pTileOutput           : register(u0);

Texture2D<float>    gs_pDepthBuffer         : register(t1);
Texture2D           gs_pAlbedoBuffer        : register(t2);
Texture2D<half2>    gs_pNormalBuffer        : register(t3);
Texture2D           gs_pInfoBuffer          : register(t4);
Buffer<uint>        gs_pTileIndices         : register(t5);

[numthreads(gs_nLightTileSize, gs_nLightTileSize, 1)]
void MainComputeProgram(uint3 nScreenPixel : SV_DispatchThreadID, uint3 nTileID : SV_GroupID, uint3 nTilePixelID : SV_GroupThreadID, uint nTilePixelIndex : SV_GroupIndex)
{
    [branch]
    if (nTilePixelIndex == 0)
    {
        g_nNumTileLights = 0;
        g_nTileMinDepth = 0x7F7FFFFF;
        g_nTileMaxDepth = 0;
    }

    float nViewDepth = gs_pDepthBuffer[nScreenPixel.xy] * gs_nCameraMaxDistance;
    uint nViewDepthInt = asuint(nViewDepth);

    GroupMemoryBarrierWithGroupSync();

    InterlockedMin(g_nTileMinDepth, nViewDepthInt);
    InterlockedMax(g_nTileMaxDepth, nViewDepthInt);

    GroupMemoryBarrierWithGroupSync();

    float nMinTileDepth = asfloat(g_nTileMinDepth);
    float nMaxTileDepth = asfloat(g_nTileMaxDepth);

    [branch]
    if (nTilePixelIndex == 0)
    {
        float2 nSize;
        gs_pDepthBuffer.GetDimensions(nSize.x, nSize.y);
        float2 nTileScale = nSize * rcp(float(2 * gs_nLightTileSize));
        float2 nTileBias = nTileScale - float2(nTileID.xy);

        float3 nXPlane = float3(gs_nProjectionMatrix[0][0] * nTileScale.x, 0.0f, nTileBias.x);
        float3 nYPlane = float3(0.0f, -gs_nProjectionMatrix[1][1] * nTileScale.y, nTileBias.y);
        float3 nZPlane = float3(0.0f, 0.0f, 1.0f);

        g_aTileFrustum[0] = float4(normalize(nZPlane - nXPlane), 0.0f),
        g_aTileFrustum[1] = float4(normalize(nZPlane + nXPlane), 0.0f),
        g_aTileFrustum[2] = float4(normalize(nZPlane - nYPlane), 0.0f),
        g_aTileFrustum[3] = float4(normalize(nZPlane + nYPlane), 0.0f),
        g_aTileFrustum[4] = float4(0.0f, 0.0f, 1.0f, -nMinTileDepth);
        g_aTileFrustum[5] = float4(0.0f, 0.0f, -1.0f, nMaxTileDepth);
    }

    GroupMemoryBarrierWithGroupSync();

    [loop]
    for (uint nLight = nTilePixelIndex; nLight < gs_nNumLights; nLight += (gs_nLightTileSize * gs_nLightTileSize))
    {
        bool bIsLightInFrustum = true;

        [unroll]
        for (uint nIndex = 0; nIndex < 6; ++nIndex)
        {
            float nDistance = dot(g_aTileFrustum[nIndex], float4(gs_aLights[nLight].m_nPosition, 1.0f));
            bIsLightInFrustum = (bIsLightInFrustum && (nDistance >= -gs_aLights[nLight].m_nRange));
        }

        [branch]
        if (bIsLightInFrustum)
        {
            uint nTileIndex;
            InterlockedAdd(g_nNumTileLights, 1, nTileIndex);
            g_aTileLightList[nTileIndex] = nLight;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    [branch]
    if (nTilePixelIndex < gs_nMaxLights)
    {
        uint nTileIndex = ((nTileID.y * gs_nDispatchXSize) + nTileID.x);
        uint nBufferIndex = ((nTileIndex * gs_nMaxLights) + nTilePixelIndex);
        uint nLightIndex = (nTilePixelIndex < g_nNumTileLights ? g_aTileLightList[nTilePixelIndex] : gs_nMaxLights);
        g_pTileOutput[nBufferIndex] = nLightIndex;
    }
}

// Diffuse & Specular Term
// http://www.gamedev.net/topic/639226-your-preferred-or-desired-brdf/
bool GetBRDF(in float3 nAlbedo, in float3 nCenterNormal, in float3 nLightNormal, in float3 nViewNormal, in float4 nInfo, out float3 nLightDiffuse, out float3 nLightSpecular)
{
    float nNormalLightAngle = saturate(dot(nCenterNormal, nLightNormal));

    [branch]
    if (nNormalLightAngle <= 0.0f)
    {
        return false;
    }

    float nRoughness = (nInfo.x * nInfo.x * 4);
    float nRoughnessSquared = (nRoughness * nRoughness);
    float nSpecular = nInfo.y;
    float nMetalness = nInfo.z;

    float3 nKs = lerp(nSpecular, nAlbedo, nMetalness);
    float3 nKd = lerp(nAlbedo, 0, nMetalness);
    float3 nFd = 1.0 - nKs;

    float nNormalViewAngle = dot(nCenterNormal, nViewNormal);
    float nNormalViewAngleSquared = (nNormalViewAngle * nNormalViewAngle);
    float nInvNormalViewAngleSquared = (1.0 - nNormalViewAngleSquared);

    float3 nHalfVector = normalize(nLightNormal + nViewNormal);
    float nNormalHalfAngle = saturate(dot(nCenterNormal, nHalfVector));
    float nLightHalfAngle = saturate(dot(nLightNormal, nHalfVector));

    float nNormalLightAngleSquared = (nNormalLightAngle * nNormalLightAngle);
    float nNormalHalfAngleSquared = (nNormalHalfAngle * nNormalHalfAngle);
    float nInvNormalLightAngleSquared = (1.0 - nNormalLightAngleSquared);

    nLightDiffuse = (nKd * nFd * gs_nReciprocalPI * saturate((1 - nRoughness) * 0.5 + 0.5 + nRoughnessSquared * (8 - nRoughness) * 0.023));

    float nCenteredNormalViewAngle = (nNormalViewAngle * 0.5 + 0.5);
    float nCenteredNormalLightAngle = (nNormalLightAngle * 0.5 + 0.5);
    nNormalViewAngleSquared = (nCenteredNormalViewAngle * nCenteredNormalViewAngle);
    nNormalLightAngleSquared = (nCenteredNormalLightAngle * nCenteredNormalLightAngle);
    nInvNormalViewAngleSquared = (1.0 - nNormalViewAngleSquared);
    nInvNormalLightAngleSquared = (1.0 - nNormalLightAngleSquared);

    float nDiffuseDelta = lerp((1 / (0.1 + nRoughness)), (-nRoughnessSquared * 2), saturate(nRoughness));
    float nDiffuseViewAngle = (1 - (pow(1 - nCenteredNormalViewAngle, 4) * nDiffuseDelta));
    float nDiffuseLightAngle = (1 - (pow(1 - nCenteredNormalLightAngle, 4) * nDiffuseDelta));
    nLightDiffuse *= (nDiffuseLightAngle * nDiffuseViewAngle * nNormalLightAngle);

    float3 nFs = (nKs + nFd * pow((1 - nLightHalfAngle), 5));
    float3 nD = (pow(nRoughness / (nNormalHalfAngleSquared * (nRoughnessSquared + (1 - nNormalHalfAngleSquared) / nNormalHalfAngleSquared)), 2) * gs_nReciprocalPI);
    nLightSpecular = (nFs * nD * nNormalLightAngle);
    return true;
}

float3 GetLightingContribution(in INPUT kInput, in float3 nAlbedo)
{
    float4 nCenterInfo = gs_pInfoBuffer.Sample(gs_pPointSampler, kInput.texcoord);

    float nCenterDepth = gs_pDepthBuffer.Sample(gs_pPointSampler, kInput.texcoord);
    float3 nCenterPosition = GetViewPosition(kInput.texcoord, nCenterDepth);
    float3 nCenterNormal = DecodeNormal(gs_pNormalBuffer.Sample(gs_pPointSampler, kInput.texcoord));

    float3 nViewNormal = -normalize(nCenterPosition);

    const uint2 nTileID = uint2(floor(kInput.position.xy / float(gs_nLightTileSize).xx));
    const uint nTileIndex = ((nTileID.y * gs_nDispatchXSize) + nTileID.x);
    const uint nBufferIndex = (nTileIndex * gs_nMaxLights);

    float3 nTotalDiffuse = 0.0f;
    float3 nTotalSpecular = 0.0f;

    [loop]
    for (uint nLightIndex = 0; nLightIndex < gs_nNumLights; nLightIndex++)
    {
        uint nLight = gs_pTileIndices[nBufferIndex + nLightIndex];

        [branch]
        if (nLight == gs_nMaxLights)
        {
            break;
        }

        float3 nLightVector = (gs_aLights[nLight].m_nPosition.xyz - nCenterPosition);
        float nDistance = length(nLightVector);
        float3 nLightNormal = normalize(nLightVector);

        float nAttenuation = (1.0f - saturate(nDistance * gs_aLights[nLight].m_nInvRange));

        [branch]
        if (nAttenuation > 0.0f)
        {
            float3 nLightDiffuse = 0.0f;
            float3 nLightSpecular = 0.0f;

            [branch]
            if (GetBRDF(nAlbedo, nCenterNormal, nLightNormal, nViewNormal, nCenterInfo, nLightDiffuse, nLightSpecular))
            {
                nTotalDiffuse += saturate(gs_aLights[nLight].m_nColor * nLightDiffuse * nAttenuation);
                nTotalSpecular += saturate(gs_aLights[nLight].m_nColor * nLightSpecular * nAttenuation);
            }
        }
    }

    return (saturate(nTotalDiffuse) + saturate(nTotalSpecular));
}

float4 MainPixelProgram(in INPUT kInput) : SV_TARGET0
{
    float3 nLighting = 0.0f;
    float4 nAlbedo = gs_pAlbedoBuffer.Sample(gs_pPointSampler, kInput.texcoord);

    [branch]
    if (nAlbedo.a < 1.0f)
    {
        nLighting = GetLightingContribution(kInput, nAlbedo.xyz);
    }

    return float4(nLighting, nAlbedo.a);
}
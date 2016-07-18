#include "GEKEngine"

#include "GEKGlobal.hlsl"
#include "GEKUtility.hlsl"

namespace Defines
{
    static const float inverseShadowTapCount = rcp(Defines::shadowTapCount);
};

float2 getTapLocation(float tap, float randomAngle)
{
    float alpha = ((tap + 0.5) * Defines::inverseShadowTapCount);
    float angle = ((alpha * (Defines::shadowSpiralCount * Math::Tau)) + randomAngle);
    return (alpha * float2(cos(angle), sin(angle)));
}

// Scalable Ambient Obscurance
// http://graphics.cs.williams.edu/papers/SAOHPG12/
float getShadowFactor(InputPixel inputPixel)
{
    float sceneDepth = Resources::depth[inputPixel.position.xy];
    float3 surfacePosition = getPositionFromProjectedDepth(inputPixel.texCoord, sceneDepth);
    float3 surfaceNormal = decodeNormal(Resources::normalBuffer[inputPixel.position.xy]);

    float randomAngle = random(inputPixel.position.xy, Engine::worldTime);
    float sampleRadius = (Defines::shadowRadius / (2.0 * surfacePosition.z * Camera::fieldOfView.x));

    float totalOcclusion = 0.0;

    [unroll]
    for (int tap = 0; tap < Defines::shadowTapCount; tap++)
    {
        float2 tapOffset = getTapLocation(tap, randomAngle);
        tapOffset *= sampleRadius;
        int2 tapCoord = tapOffset * Shader::targetSize;
        float tapDepth = Resources::depth[inputPixel.position.xy + tapCoord];
        float3 tapPosition = getPositionFromProjectedDepth(inputPixel.texCoord + tapOffset, tapDepth);

        float3 tapDelta = (tapPosition - surfacePosition);
        float deltaMagnitude = dot(tapDelta, tapDelta);
        float deltaAngle = dot(tapDelta, surfaceNormal);
        totalOcclusion += (max(0.0, (deltaAngle + (sceneDepth * 0.001))) / (deltaMagnitude + 0.1));
    }

    totalOcclusion *= (Math::Tau * Defines::shadowRadius * Defines::shadowStrength / Defines::shadowTapCount);
    totalOcclusion = saturate(1.0 - totalOcclusion);
    return totalOcclusion;
}


OutputPixel mainPixelProgram(InputPixel inputPixel)
{
    OutputPixel output;
    output.ambientOcclusionBuffer = getShadowFactor(inputPixel);
    return output;
}

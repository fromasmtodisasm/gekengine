#include "GEKFilter"

#include "GEKGlobal.hlsl"
#include "GEKUtility.hlsl"

float calculateGaussianWeight(float offset)
{
	static const float g = (1.0f / (sqrt(Math::Tau) * Defines::gaussianSigma));
	static const float d = rcp(2.0 * square(Defines::gaussianSigma));
	return (g * exp(-square(offset) * d)) / 2.0;
}

float4 mainPixelProgram(InputPixel inputPixel) : SV_TARGET0
{
	float4 finalValue = 0.0;
	float totalWeight = 0.0;

	[unroll]
	for (int2 offset = -Defines::gaussianRadius; offset.x <= Defines::gaussianRadius; offset.x++)
	{
		[unroll]
		for (offset.y = -Defines::gaussianRadius; offset.y <= Defines::gaussianRadius; offset.y++)
		{
			int2 sampleOffset = offset;
			int2 sampleCoord = (inputPixel.screen.xy + sampleOffset);
			float sampleWeight = calculateGaussianWeight(offset);

			float sampleCircleOfConfusion = Resources::circleOfConfusion[sampleCoord];
			float3 sampleColor = Resources::foregroundBuffer[sampleCoord];

			finalValue += (float4(sampleColor, saturate(-sampleCircleOfConfusion)) * sampleWeight);
			totalWeight += sampleWeight;
		}
	}

	return (finalValue * rcp(totalWeight + Math::Epsilon));
}
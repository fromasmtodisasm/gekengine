#include "GEKEngine"

#include "GEKGlobal.hlsl"
#include "GEKUtility.hlsl"

OutputPixel mainPixelProgram(InputPixel inputPixel)
{
    float4 color = (Resources::albedo.Sample(Global::linearWrapSampler, inputPixel.texCoord) * inputPixel.color);

    float depth = (inputPixel.viewPosition.z / Camera::maximumDistance);
    float weight = max(min(1.0, max(max(color.r, color.g), color.b) * color.a), color.a) * clamp(0.03 / (1e-5 + pow(depth, 4.0)), 1e-2, 3e3);

    OutputPixel outputPixel;
    outputPixel.blendBuffer = (float4((color.rgb * color.a), color.a) * weight);
    outputPixel.weightBuffer = color.a;
    return outputPixel;
}

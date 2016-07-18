#include "GEKEngine"

#include "GEKGlobal.hlsl"
#include "GEKUtility.hlsl"

// https://mynameismjp.wordpress.com/2010/04/30/a-closer-look-at-tone-mapping/
#define TechniqueNone               0
#define TechniqueLogarithmic        1
#define TechniqueExponential        2
#define TechniqueDragoLogarithmic   3
#define TechniqueReinhard           4
#define TechniqueReinhardModified   5
#define TechniqueFilmicALU          6
#define TechniqueFilmicU2           7

namespace Defines
{
    static const int techniqueMode = TechniqueFilmicALU;
    static const int autoExposureMode = 2;

    static const float luminanceSaturation = 1.0;
    static const float keyValue = 0.18;
    static const float exposure = 0.0;
    static const float whiteLevel = 5.0;
    static const float bias = 0.5;

    // Uncharted 2 Filmic ALU Defines
    static const float shoulderStrength = 0.22;
    static const float linearStrength = 0.3;
    static const float linearAngle = 0.1;
    static const float toeStrength = 0.2;
    static const float toeNumerator = 0.01;
    static const float toeDenominator = 0.3;
    static const float linearWhite = 11.2;
};

float3 getExposedColor(float3 color, float averageLuminance, float threshold, out float exposure)
{
    exposure = 0.0;
    if (Defines::autoExposureMode >= 1 && Defines::autoExposureMode <= 2)
    {
        averageLuminance = max(averageLuminance, Math::Epsilon);

        float keyValue = 0.0;
        if (Defines::autoExposureMode == 1)
        {
            keyValue = Defines::keyValue;
        }
        else if (Defines::autoExposureMode == 2)
        {
            keyValue = 1.03 - (2.0 / (2.0 + log10(averageLuminance + 1.0)));
        }

        float linearexposure = (keyValue / averageLuminance);
        exposure = log2(max(linearexposure, Math::Epsilon));
    }
    else
    {
        exposure = Defines::exposure;
    }

    exposure -= threshold;
    return exp2(exposure) * color;
}

// Logarithmic mapping
float3 getToneMapLogarithmic(float3 color)
{
    float pixelLuminance = getLuminance(color);
    float toneMappedLuminance = log10(1 + pixelLuminance) / log10(1 + Defines::whiteLevel);
    return toneMappedLuminance * pow(color / pixelLuminance, Defines::luminanceSaturation);
}

// Drago's Logarithmic mapping
float3 getToneMapDragoLogarithmic(float3 color)
{
    float pixelLuminance = getLuminance(color);
    float toneMappedLuminance = log10(1.0 + pixelLuminance);
    toneMappedLuminance /= log10(1.0 + Defines::whiteLevel);
    toneMappedLuminance /= log10(2.0 + 8.0 * ((pixelLuminance / Defines::whiteLevel) * log10(Defines::bias) / log10(0.5)));
    return toneMappedLuminance * pow(color / pixelLuminance, Defines::luminanceSaturation);
}

// Exponential mapping
float3 getToneMapExponential(float3 color)
{
    float pixelLuminance = getLuminance(color);
    float toneMappedLuminance = 1 - exp(-pixelLuminance / Defines::whiteLevel);
    return toneMappedLuminance * pow(color / pixelLuminance, Defines::luminanceSaturation);
}

// Applies Reinhard's basic tone mapping operator
float3 getToneMapReinhard(float3 color)
{
    float pixelLuminance = getLuminance(color);
    float toneMappedLuminance = pixelLuminance / (pixelLuminance + 1.0);
    return toneMappedLuminance * pow(color / pixelLuminance, Defines::luminanceSaturation);
}

// Applies Reinhard's modified tone mapping operator
float3 getToneMapReinhardModified(float3 color)
{
    float pixelLuminance = getLuminance(color);
    float toneMappedLuminance = pixelLuminance * (1.0 + pixelLuminance / (Defines::whiteLevel * Defines::whiteLevel)) / (1.0 + pixelLuminance);
    return toneMappedLuminance * pow(color / pixelLuminance, Defines::luminanceSaturation);
}

// Applies the filmic curve from John Hable's presentation
float3 getToneMapFilmicALU(float3 color)
{
    color = max(0.0, color - 0.004);
    color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);
    return pow(color, 2.2f); // result has 1/2.2 baked in
}

// Function used by the Uncharte2D tone mapping curve
float3 getUncharted2Curve(float3 x)
{
    float A = Defines::shoulderStrength;
    float B = Defines::linearStrength;
    float C = Defines::linearAngle;
    float D = Defines::toeStrength;
    float E = Defines::toeNumerator;
    float F = Defines::toeDenominator;
    return ((x*(A*x + C*B) + D*E) / (x*(A*x + B) + D*F)) - E / F;
}

// Applies the Uncharted 2 filmic tone mapping curve
float3 getToneMapFilmicU2(float3 color)
{
    float3 numerator = getUncharted2Curve(color);
    float3 denominator = getUncharted2Curve(Defines::linearWhite);
    return numerator / denominator;
}

float3 getToneMappedColor(float3 color, float averageLuminance, float threshold, out float exposure)
{
    float pixelLuminance = getLuminance(color);
    color = getExposedColor(color, averageLuminance, threshold, exposure);
    switch (Defines::techniqueMode)
    {
    case TechniqueLogarithmic:
        color = getToneMapLogarithmic(color);
        break;

    case TechniqueExponential:
        color = getToneMapExponential(color);
        break;

    case TechniqueDragoLogarithmic:
        color = getToneMapDragoLogarithmic(color);
        break;

    case TechniqueReinhard:
        color = getToneMapReinhard(color);
        break;

    case TechniqueReinhardModified:
        color = getToneMapReinhardModified(color);
        break;

    case TechniqueFilmicALU:
        color = getToneMapFilmicALU(color);
        break;

    case TechniqueFilmicU2:
        color = getToneMapFilmicU2(color);
        break;
    };

    return color;
}

float3 mainPixelProgram(InputPixel inputPixel) : SV_TARGET0
{
    float3 baseColor = Resources::finalBuffer.SampleLevel(Global::pointSampler, inputPixel.texCoord, 0);

    float exposure = 0.0;
    float averageLuminance = exp(Resources::averageLuminanceBuffer.Load(uint3(0, 0, 0)));
    float3 tonedColor = getToneMappedColor(baseColor, averageLuminance, 0.0, exposure);
    return tonedColor;
}
float getFalloff(float distance, float range)
{
    float denominator = (pow(distance, 2.0) + 1.0);
    float attenuation = pow((distance / range), 4.0);
    return (pow(saturate(1.0 - attenuation), 2.0) / denominator);
}

// Normal Distribution Function ( NDF ) or D( h )
// GGX ( Trowbridge-Reitz )
float getDistributionGGX(float alpha, float HdotN)
{
    // alpha is assumed to be roughness^2
    float alphaSquared = pow(alpha, 2.0);

    //float denominator = (HdotN * HdotN) * (alphaSquared - 1.0) + 1.0;
    float denominator = ((((HdotN * alphaSquared) - HdotN) * HdotN) + 1.0);
    return (alphaSquared / (Math::Pi * denominator * denominator));
}

float getDistributionDisneyGGX(float alpha, float HdotN)
{
    // alpha is assumed to be roughness^2
    float alphaSquared = pow(alpha, 2.0);

    float denominator = (((HdotN * HdotN) * (alphaSquared - 1.0)) + 1.0);
    return (alphaSquared / (Math::Pi * denominator));
}

float getDistribution1886GGX(float alpha, float HdotN)
{
    return (alpha / (Math::Pi * pow(((HdotN * HdotN * (alpha - 1.0)) + 1.0), 2.0)));
}

// Visibility term G( l, v, h )
// Very similar to Marmoset Toolbag 2 and gives almost the same results as Smith GGX
float getVisibilitySchlick(float VdotN, float alpha, float LdotN)
{
    float k = (alpha * 0.5);
    float schlickL = ((LdotN * (1.0 - k)) + k);
    float schlickV = ((VdotN * (1.0 - k)) + k);
    return (0.25 / (schlickL * schlickV));
    //return ((schlickL * schlickV) / (4.0 * VdotN * LdotN));
}

// see s2013_pbs_rad_notes.pdf
// Crafting a Next-Gen Material Pipeline for The Order: 1886
// this visibility function also provides some sort of back lighting
float getVisibilitySmithGGX(float VdotN, float alpha, float LdotN)
{
    float V1 = (LdotN + (sqrt(alpha + ((1.0 - alpha) * LdotN * LdotN))));
    float V2 = (VdotN + (sqrt(alpha + ((1.0 - alpha) * VdotN * VdotN))));

    // avoid too bright spots
    return (1.0 / max(V1 * V2, 0.15));
    //return (V1 * V2);
}

// Fresnel term F( v, h )
// Fnone( v, h ) = F(0?) = color
float3 getFresnelSchlick(float VdotH, float3 color)
{
    return color + (1.0 - color) * pow(1.0 - VdotH, 5.0);
}

float3 getSurfaceIrradiance(
    float3 surfaceNormal, float3 viewDirection, float VdotN,
    float3 materialAlbedo, float materialRoughness, float materialMetallic,
    float materialAlpha, float materialDisneyAlpha,
    float3 lightDirection, float3 lightRadiance)
{
    float LdotN = saturate(dot(surfaceNormal, lightDirection));

    float lambert;
    if (Defines::useHalfLambert)
    {
        // http://developer.valvesoftware.com/wiki/Half_Lambert
        float halfLdotN = ((dot(surfaceNormal, lightDirection) * 0.5) + 0.5);
        lambert = pow(halfLdotN, 2.0);
    }
    else
    {
        lambert = LdotN;
    }

    float3 halfAngleVector = normalize(lightDirection + viewDirection);
    float HdotN = saturate(dot(halfAngleVector, surfaceNormal));

    float VdotH = saturate(dot(viewDirection, halfAngleVector));

    float3 reflectedRadiance = lerp(materialAlbedo, lightRadiance, materialMetallic);

    //float D = pow(abs(HdotN), 10.0f);
    float D = getDistributionGGX(materialAlpha, HdotN);
    //float D = getDistribution1886GGX(materialAlpha, HdotN);
    //float D = getDistributionDisneyGGX(materialDisneyAlpha, HdotN);
    float G = getVisibilitySchlick(VdotN, materialAlpha, LdotN);
    //float G = getVisibilitySmithGGX(VdotN, alpham LdotN);
    float3 F = getFresnelSchlick(VdotH, reflectedRadiance);

    float specularHorizon = pow((1.0 - LdotN), 4.0);
    float3 specularRadiance = (lightRadiance - (lightRadiance * specularHorizon));
    float3 specularIrradiance = saturate(D * G * (F * (specularRadiance * lambert)));

    // see http://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
    float3 diffuseLambert = (lambert / Math::Pi);
    float3 diffuseRadiance = lerp(materialAlbedo, 0.0, materialMetallic);
    float3 diffuseIrradiance = saturate(diffuseRadiance * lightRadiance * diffuseLambert);
    /*
    Maintain energy conservation:
    Energy conservation is a restriction on the reflection model that requires that the total amount of reflected light cannot be more than the incoming light.
    http://www.rorydriscoll.com/2009/01/25/energy-conservation-in-games/
    */
    diffuseIrradiance *= (1.0 - specularIrradiance);

    return (diffuseIrradiance + specularIrradiance);
}

uint getClusterOffset(float2 screenPosition, float surfaceDepth)
{
    uint2 gridLocation = floor(screenPosition / Lights::tileSize.xy);

    float depth = (surfaceDepth - Camera::nearClip) / (Camera::farClip - Camera::nearClip);
    uint gridSlice = floor(depth * Lights::gridSize.z);

    return ((((gridSlice * Lights::gridSize.y) + gridLocation.y) * Lights::gridSize.x) + gridLocation.x);
}

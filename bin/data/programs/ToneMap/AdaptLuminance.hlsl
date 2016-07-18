#include "GEKEngine"

#include "GEKGlobal.hlsl"

namespace Defines
{
    static const float adaptionRate = 1.25;
};

[numthreads(uint(1), uint(1), 1)]
void mainPixelProgram(void)
{
    float averageLuminance = UnorderedAccess::averageLuminanceBuffer[0];
    float currentLuminance = Resources::luminanceBuffer.Load(uint3(0, 0, 9));
    averageLuminance += (currentLuminance - averageLuminance) * (1.0 - exp(-Engine::frameTime * Defines::adaptionRate));

    UnorderedAccess::averageLuminanceBuffer[0] = averageLuminance;
}
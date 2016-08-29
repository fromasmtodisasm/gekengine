#include "GEKGlobal.hlsl"

#include "GEKVisual"

namespace Particles
{
    struct Instance
    {
        float3 origin;
        float2 offset;
        float age, death;
        float angle, spin;
        float size;
        float buffer[2];
    };

    StructuredBuffer<Instance> list : register(t0);
    Texture2D<float4> colorMap : register(t1);
};

static const uint indexBuffer[6] = 
{
    0, 1, 2,
    1, 3, 2,
};

OutputVertex mainVertexProgram(InputVertex inputVertex)
{
    uint particleIndex = (inputVertex.vertexIndex / 6);
    uint cornerIndex = indexBuffer[inputVertex.vertexIndex % 6];

    Particles::Instance instanceData = Particles::list[particleIndex];

    float sinAngle, cosAngle;
    sincos(instanceData.angle, sinAngle, cosAngle);
    float3x3 angle = float3x3(cosAngle, sinAngle, 0.0,
                             -sinAngle, cosAngle, 0.0,
                              0.0, 0.0, 1.0);

    float3 edge;
    float2 normal;
    normal.x = edge.x = ((cornerIndex % 2) ? 1.0 : -1.0);
    normal.y = edge.y = ((cornerIndex & 2) ? -1.0 : 1.0);
    edge.z = 0.0f;
    edge = (mul(angle, edge) * instanceData.size);

    float age = (instanceData.age > 0.0 ? (instanceData.age / instanceData.death) : 0.0);
    float wave = sin(age * Math::Pi);

    float3 position;
    position.x = (instanceData.origin.x + (instanceData.offset.x * wave));
    position.y = (instanceData.origin.y + instanceData.age);
    position.z = (instanceData.origin.z + (instanceData.offset.y * wave));

    OutputVertex outputVertex;
    outputVertex.position = (edge + mul(Camera::viewMatrix, float4(position, 1.0)).xyz);
    outputVertex.normal = normalize(float3(normal.xy, -1.0));
    outputVertex.texCoord.x = ((cornerIndex % 2) ? 1.0 : 0.0);
    outputVertex.texCoord.y = ((cornerIndex & 2) ? 1.0 : 0.0);
    return getProjection(outputVertex);
}

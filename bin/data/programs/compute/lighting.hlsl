cbuffer ENGINEBUFFER                    : register(b0)
{
    float2   gs_nCameraSize             : packoffset(c0);
    float2   gs_nCameraView             : packoffset(c0.z);
    float    gs_nCameraViewDistance     : packoffset(c1);
    float3   gs_nCameraPosition         : packoffset(c1.y);
    float4x4 gs_nViewMatrix             : packoffset(c2);
    float4x4 gs_nProjectionMatrix       : packoffset(c6);
    float4x4 gs_nTransformMatrix        : packoffset(c10);
};

struct LIGHT
{
    float3 m_nPosition;
    float  m_nRange;
    float3 m_nColor;
    float  m_nInvRange;
};

StructuredBuffer<LIGHT> gs_aLights      : register(t0);

_INSERT_COMPUTE_PROGRAM
cbuffer ENGINEBUFFER                    : register(b0)
{
    float4x4 gs_nViewMatrix;
    float4x4 gs_nProjectionMatrix;
    float4x4 gs_nTransformMatrix;
    float3   gs_nCameraPosition;
    float    gs_nCameraViewDistance;
    float2   gs_nCameraView;
    float2   gs_nCameraSize;
};

cbuffer ORTHOBUFFER                     : register(b1)
{
    float4x4 gs_nOrthoMatrix;
};

struct VERTEX
{
    float2 position                     : POSITION;
	float2 view							: TEXCOORD0;
};

struct PIXEL
{
    float4 position                     : SV_POSITION;
    float2 texcoord                     : TEXCOORD0;
	float3 view							: TEXCOORD1;
};

PIXEL MainVertexProgram(in VERTEX kVertex)
{
    PIXEL kPixel;
    kPixel.position = mul(gs_nOrthoMatrix, float4(kVertex.position, 0.0f, 1.0f));
    kPixel.texcoord = kVertex.position.xy;
	kPixel.view.xy = (kVertex.view * gs_nCameraView);
	kPixel.view.z = 1.0f;
    return kPixel;
}

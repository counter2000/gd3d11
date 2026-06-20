//--------------------------------------------------------------------------------------
// Depth of Field Focus Resolve
// Uses the configured deterministic focus distance. This keeps Kirides' DoF
// pipeline intact but removes temporal auto-focus pumping.
//--------------------------------------------------------------------------------------

#include "DepthReconstruction.h"

cbuffer DepthOfFieldConstantBuffer : register( b0 )
{
    float DoF_FocusDistance;
    float DoF_FocusRange;
    float DoF_BokehRadius;
    float DoF_MaxBlur;

    float4 DoF_ProjParams;
    float DoF_NearPlane;
    float DoF_FarPlane;
    float DoF_Pad;
    float DoF_Pad2;
};

SamplerState SS_Linear : register( s0 );
Texture2D TX_Depth : register( t0 );
Texture2D TX_PrevFocus : register( t1 );
struct PS_INPUT
{
    float2 vTexcoord : TEXCOORD0;
    float3 vEyeRay   : TEXCOORD1;
    float4 vPosition : SV_POSITION;
};

float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
    return float4( max( DoF_FocusDistance, DoF_NearPlane ), 0, 0, 1 );
}
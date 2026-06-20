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
RWTexture2D<float> OutputFocus : register( u0 );

[numthreads(1, 1, 1)]
void CSMain( uint3 DTid : SV_DispatchThreadID )
{
    OutputFocus[uint2(0,0)] = max( DoF_FocusDistance, DoF_NearPlane );
}
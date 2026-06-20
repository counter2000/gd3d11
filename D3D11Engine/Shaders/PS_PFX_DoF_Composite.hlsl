//--------------------------------------------------------------------------------------
// Depth of Field - Full-res composite pass
// Reads full-res scene + depth, upsampled half-res bokeh blur, and focus texture
// Blends sharp and blurred based on per-pixel CoC
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
Texture2D TX_Scene : register( t0 );   // Full-res sharp scene
Texture2D TX_Blur  : register( t1 );   // Half-res bokeh (rgb=blur, a=CoC)
Texture2D TX_Depth : register( t2 );   // Full-res hardware depth
Texture2D TX_Focus : register( t3 );   // 1x1 smoothed focus depth

struct PS_INPUT
{
    float2 vTexcoord : TEXCOORD0;
    float3 vEyeRay   : TEXCOORD1;
    float4 vPosition : SV_POSITION;
};

float LinearizeDepth( float d )
{
    return LinearizeDepthReverseZInfinite( d );
}

bool IsSkyDepth( float d )
{
    return d <= 1e-7f;
}

float ComputeCoCFromDepth( float d, float focusDepth )
{
    if ( IsSkyDepth( d ) )
        return 0.0f;

    return saturate( ( LinearizeDepth( d ) - focusDepth ) / DoF_FocusRange );
}

void AccumulateSkyEdgeSample(float2 sampleUV, float focusDepth, float falloff, inout float3 colorAccum, inout float weightAccum)
{
    float depth = TX_Depth.Sample( SS_Linear, sampleUV ).r;
    float coc = ComputeCoCFromDepth( depth, focusDepth );
    float4 blur = TX_Blur.Sample( SS_Linear, sampleUV );
    float weight = smoothstep(0.18f, 0.65f, coc) * smoothstep(0.05f, 0.60f, blur.a) * falloff;
    colorAccum += blur.rgb * weight;
    weightAccum += weight;
}

float4 GetSkyEdgeBlurSample(float2 texcoord, float2 dtexel, float focusDepth)
{
    float3 colorAccum = 0.0f;
    float weightAccum = 0.0f;

    AccumulateSkyEdgeSample(texcoord + float2(-dtexel.x, 0), focusDepth, 1.0f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2( dtexel.x, 0), focusDepth, 1.0f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2(0, -dtexel.y), focusDepth, 1.0f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2(0,  dtexel.y), focusDepth, 1.0f, colorAccum, weightAccum);

    AccumulateSkyEdgeSample(texcoord + float2(-dtexel.x, -dtexel.y) * 2.0f, focusDepth, 0.85f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2( dtexel.x, -dtexel.y) * 2.0f, focusDepth, 0.85f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2(-dtexel.x,  dtexel.y) * 2.0f, focusDepth, 0.85f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2( dtexel.x,  dtexel.y) * 2.0f, focusDepth, 0.85f, colorAccum, weightAccum);

    AccumulateSkyEdgeSample(texcoord + float2(-dtexel.x, 0) * 5.0f, focusDepth, 0.55f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2( dtexel.x, 0) * 5.0f, focusDepth, 0.55f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2(0, -dtexel.y) * 5.0f, focusDepth, 0.55f, colorAccum, weightAccum);
    AccumulateSkyEdgeSample(texcoord + float2(0,  dtexel.y) * 5.0f, focusDepth, 0.55f, colorAccum, weightAccum);

    float blend = smoothstep(0.04f, 0.35f, weightAccum);
    return float4(colorAccum / max(weightAccum, 0.001f), blend);
}
float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
    float3 sharpColor = TX_Scene.Sample( SS_Linear, Input.vTexcoord ).rgb;

    float focusDepth = TX_Focus.SampleLevel( SS_Linear, float2( 0.5, 0.5 ), 0 ).r;

    // Compute CoC at center and 4 neighbours, use the minimum.
    // This erodes the blur zone by 1 pixel at depth discontinuities,
    // preventing bilinear upsample of the half-res blur from fattening
    // thin features like leaves and fences.
    float2 depthSize;
    TX_Depth.GetDimensions( depthSize.x, depthSize.y );
    float2 dtexel = 1.0 / depthSize;
    float depthC = TX_Depth.Sample( SS_Linear, Input.vTexcoord ).r;
    float4 blurSample = TX_Blur.Sample( SS_Linear, Input.vTexcoord );
    if ( IsSkyDepth( depthC ) )
    {
        float4 skyEdgeBlur = GetSkyEdgeBlurSample( Input.vTexcoord, dtexel, focusDepth );
        return float4( lerp( sharpColor, skyEdgeBlur.rgb, skyEdgeBlur.a ), 1.0 );
    }

    float cocC = ComputeCoCFromDepth( depthC, focusDepth );
    float cocL = ComputeCoCFromDepth( TX_Depth.Sample( SS_Linear, Input.vTexcoord + float2( -dtexel.x, 0 ) ).r, focusDepth );
    float cocR = ComputeCoCFromDepth( TX_Depth.Sample( SS_Linear, Input.vTexcoord + float2(  dtexel.x, 0 ) ).r, focusDepth );
    float cocU = ComputeCoCFromDepth( TX_Depth.Sample( SS_Linear, Input.vTexcoord + float2( 0, -dtexel.y ) ).r, focusDepth );
    float cocD = ComputeCoCFromDepth( TX_Depth.Sample( SS_Linear, Input.vTexcoord + float2( 0,  dtexel.y ) ).r, focusDepth );

    float minCoC = min( min( cocC, cocL ), min( cocR, min( cocU, cocD ) ) );

    // Bilinear-upsampled half-res bokeh blur

    float blendFactor = smoothstep( 0.0, 1.0, minCoC );
    float3 finalColor = lerp( sharpColor, blurSample.rgb, blendFactor );

    return float4( finalColor, 1.0 );
}

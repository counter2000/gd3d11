//--------------------------------------------------------------------------------------
// Compute Shader - Depth of Field Full-res Composite
// Reads full-res scene + depth, upsampled half-res bokeh blur, and focus texture
// Blends sharp and blurred based on per-pixel CoC, writes to UAV
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

RWTexture2D<float4> OutputComposite : register( u0 );

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
    float depth = TX_Depth.SampleLevel( SS_Linear, sampleUV, 0 ).r;
    float coc = ComputeCoCFromDepth( depth, focusDepth );
    float4 blur = TX_Blur.SampleLevel( SS_Linear, sampleUV, 0 );
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
[numthreads(8, 8, 1)]
void CSMain( uint3 DTid : SV_DispatchThreadID )
{
    uint2 outSize;
    OutputComposite.GetDimensions( outSize.x, outSize.y );

    if ( DTid.x >= outSize.x || DTid.y >= outSize.y )
        return;

    float2 texcoord = ( float2( DTid.xy ) + 0.5 ) / float2( outSize );

    float3 sharpColor = TX_Scene.SampleLevel( SS_Linear, texcoord, 0 ).rgb;

    float focusDepth = TX_Focus.SampleLevel( SS_Linear, float2( 0.5, 0.5 ), 0 ).r;

    // Compute CoC at center and 4 neighbours, use the minimum.
    // This erodes the blur zone by 1 pixel at depth discontinuities.
    float2 depthSize;
    TX_Depth.GetDimensions( depthSize.x, depthSize.y );
    float2 dtexel = 1.0 / depthSize;
    float depthC = TX_Depth.SampleLevel( SS_Linear, texcoord, 0 ).r;
    float4 blurSample = TX_Blur.SampleLevel( SS_Linear, texcoord, 0 );
    if ( IsSkyDepth( depthC ) )
    {
        float4 skyEdgeBlur = GetSkyEdgeBlurSample( texcoord, dtexel, focusDepth );
        OutputComposite[DTid.xy] = float4( lerp( sharpColor, skyEdgeBlur.rgb, skyEdgeBlur.a ), 1.0 );
        return;
    }

    float cocC = ComputeCoCFromDepth( depthC, focusDepth );
    float cocL = ComputeCoCFromDepth( TX_Depth.SampleLevel( SS_Linear, texcoord + float2( -dtexel.x, 0 ), 0 ).r, focusDepth );
    float cocR = ComputeCoCFromDepth( TX_Depth.SampleLevel( SS_Linear, texcoord + float2(  dtexel.x, 0 ), 0 ).r, focusDepth );
    float cocU = ComputeCoCFromDepth( TX_Depth.SampleLevel( SS_Linear, texcoord + float2( 0, -dtexel.y ), 0 ).r, focusDepth );
    float cocD = ComputeCoCFromDepth( TX_Depth.SampleLevel( SS_Linear, texcoord + float2( 0,  dtexel.y ), 0 ).r, focusDepth );

    float minCoC = min( min( cocC, cocL ), min( cocR, min( cocU, cocD ) ) );

    // Bilinear-upsampled half-res bokeh blur

    float blendFactor = smoothstep( 0.0, 1.0, minCoC );
    float3 finalColor = lerp( sharpColor, blurSample.rgb, blendFactor );

    OutputComposite[DTid.xy] = float4( finalColor, 1.0 );
}

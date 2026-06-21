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

static const int SKY_EDGE_SAMPLE_COUNT = 24;

float2 GetSkyEdgeSpiralSample(int index)
{
    float radius = sqrt((float(index) + 0.5f) / float(SKY_EDGE_SAMPLE_COUNT));
    float angle = float(index) * 2.39996323f;
    return float2(cos(angle), sin(angle)) * radius;
}

float4 GetSkyEdgeBlurSample(float2 texcoord, float2 dtexel, float focusDepth)
{
    float3 colorAccum = 0.0f;
    float coverageAccum = 0.0f;
    float kernelAccum = 0.0f;
    float edgeRadius = clamp(max(DoF_BokehRadius, DoF_MaxBlur) * 0.22f, 2.0f, 10.0f);

    [unroll]
    for (int i = 0; i < SKY_EDGE_SAMPLE_COUNT; ++i)
    {
        float2 offset = GetSkyEdgeSpiralSample(i);
        float2 sampleUV = texcoord + offset * edgeRadius * dtexel;
        float depth = TX_Depth.SampleLevel(SS_Linear, sampleUV, 0).r;
        float coc = ComputeCoCFromDepth(depth, focusDepth);
        float4 blur = TX_Blur.SampleLevel(SS_Linear, sampleUV, 0);
        float radialWeight = exp(-dot(offset, offset) * 2.4f);
        float geometryWeight = IsSkyDepth(depth) ? 0.0f : smoothstep(0.12f, 0.65f, coc);
        float sampleWeight = radialWeight * geometryWeight;

        colorAccum += blur.rgb * sampleWeight;
        coverageAccum += sampleWeight;
        kernelAccum += radialWeight;
    }

    float coverage = saturate(coverageAccum / max(kernelAccum, 0.001f) * 2.2f);
    float blend = smoothstep(0.02f, 0.85f, coverage);
    return float4(colorAccum / max(coverageAccum, 0.001f), blend);
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
    float depthL = TX_Depth.SampleLevel( SS_Linear, texcoord + float2( -dtexel.x, 0 ), 0 ).r;
    float depthR = TX_Depth.SampleLevel( SS_Linear, texcoord + float2(  dtexel.x, 0 ), 0 ).r;
    float depthU = TX_Depth.SampleLevel( SS_Linear, texcoord + float2( 0, -dtexel.y ), 0 ).r;
    float depthD = TX_Depth.SampleLevel( SS_Linear, texcoord + float2( 0,  dtexel.y ), 0 ).r;
    float cocL = IsSkyDepth( depthL ) ? cocC : ComputeCoCFromDepth( depthL, focusDepth );
    float cocR = IsSkyDepth( depthR ) ? cocC : ComputeCoCFromDepth( depthR, focusDepth );
    float cocU = IsSkyDepth( depthU ) ? cocC : ComputeCoCFromDepth( depthU, focusDepth );
    float cocD = IsSkyDepth( depthD ) ? cocC : ComputeCoCFromDepth( depthD, focusDepth );

    float2 inwardShift = float2(
        (IsSkyDepth(depthL) ? 1.0f : 0.0f) - (IsSkyDepth(depthR) ? 1.0f : 0.0f),
        (IsSkyDepth(depthU) ? 1.0f : 0.0f) - (IsSkyDepth(depthD) ? 1.0f : 0.0f));
    float inwardLength = length(inwardShift);
    if ( inwardLength > 0.0f )
    {
        float shiftStrength = saturate((DoF_BokehRadius - 5.0f) / 27.0f);
        float2 inwardDirection = inwardShift / inwardLength;
        float2 shiftedBlurUV = texcoord + inwardDirection * dtexel * shiftStrength;
        float2 edgeTangent = float2(-inwardDirection.y, inwardDirection.x) * dtexel * 0.5f;
        blurSample = 0.5f * (TX_Blur.SampleLevel( SS_Linear, shiftedBlurUV - edgeTangent, 0 )
            + TX_Blur.SampleLevel( SS_Linear, shiftedBlurUV + edgeTangent, 0 ));
    }

    float minCoC = min( min( cocC, cocL ), min( cocR, min( cocU, cocD ) ) );

    // Bilinear-upsampled half-res bokeh blur

    float blendFactor = smoothstep( 0.0, 1.0, minCoC );
    float3 finalColor = lerp( sharpColor, blurSample.rgb, blendFactor );

    OutputComposite[DTid.xy] = float4( finalColor, 1.0 );
}

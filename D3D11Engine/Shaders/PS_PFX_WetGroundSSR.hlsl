//--------------------------------------------------------------------------------------
// Screen-space reflections for rain-wet ground surfaces
//--------------------------------------------------------------------------------------

#include "DepthReconstruction.h"
#include "DS_Defines.h"

cbuffer WetGroundSSRConstantBuffer : register(b0)
{
    float4 WG_ProjParams;
    matrix WG_InvView;
    matrix WG_ViewProj;
    matrix WG_RainViewProj;

    float3 WG_CameraPosition;
    float WG_Wetness;

    float2 WG_InvResolution;
    float WG_Strength;
    float WG_Time;
};

SamplerState SS_Linear : register(s0);
SamplerComparisonState SS_Comp : register(s1);
Texture2D TX_Scene : register(t0);
Texture2D TX_Depth : register(t1);
Texture2D TX_Normals : register(t2);
Texture2D TX_RainShadow : register(t3);
Texture2D TX_Distortion : register(t4);
Texture2D TX_WaterMask : register(t5);

struct PS_INPUT
{
    float2 vTexcoord : TEXCOORD0;
    float3 vEyeRay : TEXCOORD1;
    float4 vPosition : SV_POSITION;
};

float3 ReconstructVS(float depth, float2 uv)
{
    return ReconstructVSPositionFromDepthReverseZInfinite(depth, uv, WG_ProjParams.xy) * WG_ProjParams.z;
}

float GetRainExposure(float3 wsPosition)
{
    float4 shadowPosition = mul(float4(wsPosition, 1.0f), WG_RainViewProj);
    float2 shadowUV = shadowPosition.xy * float2(0.5f, -0.5f) + 0.5f;
    if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f))
        return 0.0f;

    return TX_RainShadow.SampleCmpLevelZero(SS_Comp, shadowUV, shadowPosition.z - 0.0001f);
}

float3 SampleRoughReflection(float2 uv, float2 distortion)
{
    float2 spread = WG_InvResolution * 2.0f + abs(distortion) * 0.0015f;
    float3 color = TX_Scene.SampleLevel(SS_Linear, uv, 0).rgb * 0.40f;
    color += TX_Scene.SampleLevel(SS_Linear, uv + float2(spread.x, 0), 0).rgb * 0.15f;
    color += TX_Scene.SampleLevel(SS_Linear, uv - float2(spread.x, 0), 0).rgb * 0.15f;
    color += TX_Scene.SampleLevel(SS_Linear, uv + float2(0, spread.y), 0).rgb * 0.15f;
    color += TX_Scene.SampleLevel(SS_Linear, uv - float2(0, spread.y), 0).rgb * 0.15f;
    return color;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    float2 uv = input.vTexcoord;
    float3 sceneColor = TX_Scene.SampleLevel(SS_Linear, uv, 0).rgb;
    if (TX_WaterMask.SampleLevel(SS_Linear, uv, 0).r > 0.5f)
        return float4(sceneColor, 1.0f);

    float depth = TX_Depth.SampleLevel(SS_Linear, uv, 0).r;
    if (depth <= 1e-7f || WG_Wetness <= 0.001f || WG_Strength <= 0.001f)
        return float4(sceneColor, 1.0f);

    float3 vsPosition = ReconstructVS(depth, uv);
    float3 wsPosition = mul(float4(vsPosition, 1.0f), WG_InvView).xyz;
    float3 vsNormal = DecodeNormalGBuffer(TX_Normals.SampleLevel(SS_Linear, uv, 0).xy);
    float3 wsNormal = normalize(mul(float4(vsNormal, 0.0f), WG_InvView).xyz);

    float upwardMask = smoothstep(0.45f, 0.88f, wsNormal.y);
    if (upwardMask <= 0.01f)
        return float4(sceneColor, 1.0f);

    float rainExposure = GetRainExposure(wsPosition);
    float wetMask = upwardMask * rainExposure * saturate(WG_Wetness);
    if (wetMask <= 0.01f)
        return float4(sceneColor, 1.0f);

    float2 wetUV = wsPosition.xz / 1100.0f;
    float2 distortion = TX_Distortion.SampleLevel(SS_Linear, wetUV + WG_Time * float2(0.013f, -0.009f), 0).xy * 2.0f - 1.0f;
    distortion += (TX_Distortion.SampleLevel(SS_Linear, wetUV * 0.63f + WG_Time * float2(-0.007f, 0.011f), 0).xy * 2.0f - 1.0f) * 0.5f;
    float3 wetNormal = normalize(wsNormal + float3(distortion.x, 0.0f, distortion.y) * 0.10f);

    float3 viewRay = normalize(wsPosition - WG_CameraPosition);
    float3 rayDirection = normalize(reflect(viewRay, wetNormal));
    if (rayDirection.y <= 0.015f)
        return float4(sceneColor, 1.0f);

    float3 rayPosition = wsPosition + wetNormal * 12.0f;
    float stepSize = 35.0f;
    float2 hitUV = 0.0f;
    float hitWeight = 0.0f;

    [loop]
    for (int i = 0; i < 16; ++i)
    {
        rayPosition += rayDirection * stepSize;
        stepSize *= 1.15f;

        float4 projected = mul(float4(rayPosition, 1.0f), WG_ViewProj);
        if (projected.w <= 0.0f)
            break;

        projected.xyz /= projected.w;
        float2 sampleUV = projected.xy * float2(0.5f, -0.5f) + 0.5f;
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f) || projected.z < 0.0f || projected.z > 1.0f)
            break;

        float sampleDepth = TX_Depth.SampleLevel(SS_Linear, sampleUV, 0).r;
        if (sampleDepth <= 1e-7f)
            continue;

        float sampleZ = WG_ProjParams.z / (sampleDepth - WG_ProjParams.w);
        float depthDifference = projected.w - sampleZ;
        if (depthDifference > 0.0f && depthDifference < stepSize * 2.0f)
        {
            hitUV = sampleUV;
            float edge = max(abs(sampleUV.x - 0.5f), abs(sampleUV.y - 0.5f)) * 2.0f;
            hitWeight = 1.0f - smoothstep(0.76f, 1.0f, edge);
            break;
        }
    }

    if (hitWeight <= 0.0f)
        return float4(sceneColor, 1.0f);

    float3 reflectedColor = SampleRoughReflection(hitUV, distortion);
    float reflectionLuma = dot(reflectedColor, float3(0.2126f, 0.7152f, 0.0722f));
    reflectedColor *= rcp(1.0f + max(0.0f, reflectionLuma - 1.0f) * 0.7f);

    float fresnel = pow(1.0f - saturate(dot(-viewRay, wetNormal)), 3.0f);
    float reflectionBlend = wetMask * hitWeight * lerp(0.10f, 0.48f, fresnel) * WG_Strength;
    return float4(lerp(sceneColor, reflectedColor, saturate(reflectionBlend)), 1.0f);
}
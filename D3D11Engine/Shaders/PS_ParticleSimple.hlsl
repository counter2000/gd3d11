//--------------------------------------------------------------------------------------
// Non-additive particle pixel shader with scene lighting adaptation.
//--------------------------------------------------------------------------------------
#include <AtmosphericScattering.h>

SamplerState SS_Linear : register( s0 );
Texture2D TX_Texture0 : register( t0 );
#ifndef USE_FFDATA
Texture2D TX_Depth : register( t3 );

cbuffer RefractionInfo : register( b0 )
{
    float4x4 RI_Projection;
    float2 RI_ViewportSize;
    float RI_Time;
    float RI_Far;
    float3 RI_CameraPosition;
    float RI_Pad2;
};
#endif

#ifdef USE_FFDATA
struct FFData {
    float4 textureFactor;
};
cbuffer cbFFData : register( b0 ) {
    FFData cbFFData;
};
#endif

struct PS_INPUT
{
    float2 vTexcoord        : TEXCOORD0;
    float2 vTexcoord2       : TEXCOORD1;
    float4 vDiffuse         : TEXCOORD2;
    float3 vNormalVS        : TEXCOORD4;
    float3 vViewPosition    : TEXCOORD5;
    float4 vCurrClipPos     : TEXCOORD6;
    float4 vPrevClipPos     : TEXCOORD7;
    float vParticleLightingScale : TEXCOORD8;
    float4 vPosition        : SV_POSITION;
};

float3 AdaptParticleLighting(float3 rgb, float particleLightingScale)
{
    float luma = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float warmEmission = saturate((rgb.r - max(rgb.g, rgb.b)) * 3.0f);
    float emissiveGuess = saturate((luma - 0.62f) * 2.0f + warmEmission * 0.75f);
    float night = saturate((-AC_LightPos.y + 0.08f) * 2.5f);
    float rain = max(saturate(AC_RainFXWeight), saturate(AC_SceneWettness));
    float nonEmissiveDim = lerp(1.0f, 0.24f, night) * lerp(1.0f, 0.78f, rain);
    float emissiveDim = lerp(1.0f, 0.70f, night * rain);
    float factor = lerp(nonEmissiveDim, emissiveDim, emissiveGuess);
    return rgb * lerp(1.0f, factor, saturate(AC_EnableParticleLighting * AC_ParticleLightingStrength) * saturate(particleLightingScale));
}

float SoftParticleFade( PS_INPUT Input, float3 rgb )
{
#ifndef USE_FFDATA
    float2 screenUV = Input.vPosition.xy / RI_ViewportSize;
    float sceneDepth = TX_Depth.Sample( SS_Linear, saturate(screenUV) ).r;
    if ( sceneDepth <= 1e-7f || Input.vViewPosition.z <= 0.0f )
        return 1.0f;

    float sceneViewDepth = RI_Projection._43 / (sceneDepth - RI_Projection._33);
    float depthDifference = sceneViewDepth - Input.vViewPosition.z;
    if ( depthDifference <= 0.0f )
        return 0.0f;

    float luma = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float warmEmission = saturate((rgb.r - max(rgb.g, rgb.b)) * 3.0f);
    float emissiveGuess = saturate((luma - 0.45f) * 2.4f + warmEmission * 0.85f);

    float smokeFade = smoothstep(0.0f, 1.0f, saturate(depthDifference / 45.0f));
    float fireEdgeFade = smoothstep(0.0f, 1.0f, saturate(depthDifference / 10.0f));
    float fireFade = max(fireEdgeFade, 0.78f);
    return lerp(smokeFade, fireFade, emissiveGuess);
#else
    return 1.0f;
#endif
}

float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
    float4 color = TX_Texture0.Sample(SS_Linear, Input.vTexcoord);
    color *= Input.vDiffuse;
#ifdef USE_FFDATA
    color *= cbFFData.textureFactor;
#endif
    color.a *= SoftParticleFade(Input, color.rgb);
    color.rgb = AdaptParticleLighting(color.rgb, Input.vParticleLightingScale);
    return color;
}

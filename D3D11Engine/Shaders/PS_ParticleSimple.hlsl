//--------------------------------------------------------------------------------------
// Non-additive particle pixel shader with scene lighting adaptation.
//--------------------------------------------------------------------------------------
#include <AtmosphericScattering.h>

SamplerState SS_Linear : register( s0 );
Texture2D TX_Texture0 : register( t0 );

struct PS_INPUT
{
    float2 vTexcoord        : TEXCOORD0;
    float2 vTexcoord2       : TEXCOORD1;
    float4 vDiffuse         : TEXCOORD2;
    float3 vNormalVS        : TEXCOORD4;
    float3 vViewPosition    : TEXCOORD5;
    float4 vCurrClipPos     : TEXCOORD6;
    float4 vPrevClipPos     : TEXCOORD7;
    float4 vPosition        : SV_POSITION;
};

float3 AdaptParticleLighting(float3 rgb)
{
    float luma = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float warmEmission = saturate((rgb.r - max(rgb.g, rgb.b)) * 3.0f);
    float emissiveGuess = saturate((luma - 0.62f) * 2.0f + warmEmission * 0.75f);
    float night = saturate((-AC_LightPos.y + 0.08f) * 2.5f);
    float rain = max(saturate(AC_RainFXWeight), saturate(AC_SceneWettness));
    float nonEmissiveDim = lerp(1.0f, 0.24f, night) * lerp(1.0f, 0.78f, rain);
    float emissiveDim = lerp(1.0f, 0.70f, night * rain);
    float factor = lerp(nonEmissiveDim, emissiveDim, emissiveGuess);
    return rgb * lerp(1.0f, factor, saturate(AC_EnableParticleLighting * AC_ParticleLightingStrength));
}

float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
    float4 color = TX_Texture0.Sample(SS_Linear, Input.vTexcoord);
    color *= Input.vDiffuse;
    color.rgb = AdaptParticleLighting(color.rgb);
    return color;
}
//--------------------------------------------------------------------------------------
// World/VOB-Pixelshader for G2D3D11 by Degenerated
//--------------------------------------------------------------------------------------
#include <AtmosphericScattering.h>

cbuffer RefractionInfo : register( b0 )
{
	float4x4 RI_Projection;
	float2 RI_ViewportSize;
	float RI_Time;
	float RI_Far;
	
	float3 RI_CameraPosition;
	float RI_Pad2;
};

//--------------------------------------------------------------------------------------
// Textures and Samplers
//--------------------------------------------------------------------------------------
SamplerState SS_Linear : register( s0 );
Texture2D	TX_Texture0 : register( t0 );

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct PS_INPUT
{
	float2 vTexcoord		: TEXCOORD0;
	float2 vTexcoord2		: TEXCOORD1;
	float4 vDiffuse			: TEXCOORD2;
	float3 vNormalVS		: TEXCOORD4;
	float3 vViewPosition	: TEXCOORD5;
	float4 vCurrClipPos     : TEXCOORD6;
	float4 vPrevClipPos     : TEXCOORD7;
    float vParticleLightingScale : TEXCOORD8;
	float4 vPosition		: SV_POSITION;
};

struct PS_OUTPUT
{
	float4 gb0 : SV_TARGET0;
	float4 gb1 : SV_TARGET1;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
PS_OUTPUT PSMain( PS_INPUT Input )
{
	float4 color = TX_Texture0.Sample(SS_Linear, Input.vTexcoord);
	color *= Input.vDiffuse;

	float particleLuma = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
	float warmEmission = saturate((color.r - max(color.g, color.b)) * 3.0f);
	float emissiveGuess = saturate((particleLuma - 0.62f) * 2.0f + warmEmission * 0.75f);
	float nightParticle = saturate((-AC_LightPos.y + 0.08f) * 2.5f);
	float rainParticle = max(saturate(AC_RainFXWeight), saturate(AC_SceneWettness));
	float nonEmissiveDim = lerp(1.0f, 0.28f, nightParticle) * lerp(1.0f, 0.78f, rainParticle);
	float emissiveDim = lerp(1.0f, 0.72f, nightParticle * rainParticle);
	float particleLighting = lerp(nonEmissiveDim, emissiveDim, emissiveGuess);
	color.rgb *= lerp(1.0f, particleLighting, saturate(AC_EnableParticleLighting * AC_ParticleLightingStrength * Input.vParticleLightingScale));
	
	PS_OUTPUT o;
	// Store particle color
	o.gb0 = color;
	
	// Center the UV
	float2 uvCenter = Input.vTexcoord - 0.5f;
	float weight = dot(color.rgb, float3(0.333f, 0.333f, 0.333f)) * pow(color.a, 1/4.0f);
	weight *= 2.5f; // Scale the distortion down a bit
	weight *= min(1.0f, Input.vPosition.z * 8.0f);
	
	// Store the direction to the center of the uv-plane as distortion
	o.gb1 = float4(uvCenter * float2(-1,1) * weight, 0, color.a);
	return o;
}


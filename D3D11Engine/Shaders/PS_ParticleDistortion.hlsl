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
Texture2D	TX_Depth : register( t3 );

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

float SoftParticleFade( PS_INPUT Input )
{
	if (AC_EnableSoftParticles < 0.5f || AC_SoftParticleStrength <= 0.001f)
		return 1.0f;

	float2 screenUV = Input.vPosition.xy / RI_ViewportSize;
	float sceneDepth = TX_Depth.Sample(SS_Linear, saturate(screenUV)).r;
	if (sceneDepth <= 1e-7f || Input.vViewPosition.z <= 0.0f)
		return 1.0f;

	float sceneViewDepth = RI_Projection._43 / (sceneDepth - RI_Projection._33);
	float depthDifference = sceneViewDepth - Input.vViewPosition.z;
	if (depthDifference <= 0.0f)
		return 0.0f;

	float fadeDistance = (Input.vParticleLightingScale < 0.0f ? 10.0f : 45.0f) * AC_SoftParticleStrength;
	return smoothstep(0.0f, 1.0f, saturate(depthDifference / fadeDistance));
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
PS_OUTPUT PSMain( PS_INPUT Input )
{
	float4 color = TX_Texture0.Sample(SS_Linear, Input.vTexcoord);
	color *= Input.vDiffuse;

	float softFade = SoftParticleFade(Input);
	color *= softFade;

	if (Input.vParticleLightingScale >= 0.0f)
	{
		float nightParticle = saturate((-AC_LightPos.y + 0.08f) * 2.5f);
		float rainParticle = max(saturate(AC_RainFXWeight), saturate(AC_SceneWettness));
		float nonEmissiveDim = lerp(1.0f, 0.28f, nightParticle) * lerp(1.0f, 0.78f, rainParticle);
		float lightingStrength = saturate(AC_EnableParticleLighting * AC_ParticleLightingStrength) * saturate(Input.vParticleLightingScale);
		color.rgb *= lerp(1.0f, nonEmissiveDim, lightingStrength);
	}
	
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


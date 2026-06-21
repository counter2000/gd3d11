//--------------------------------------------------------------------------------------
// World/VOB-Pixelshader for G2D3D11 by Degenerated
//--------------------------------------------------------------------------------------

#include <AtmosphericScattering.h>
#include "DepthReconstruction.h"

cbuffer PFXBuffer : register( b0 )
{
	float4 HF_ProjParams; // x = 1/P._11, y = 1/P._22, z = P._43, w = P._33
	matrix HF_InvView;
	float3 HF_CameraPosition;
	float HF_FogHeight;

	float HF_HeightFalloff;
	float HF_GlobalDensity;
	float HF_WeightZNear;
	float HF_WeightZFar;

	float3 HF_FogColorMod;
	float HF_pad2;

	float2 HF_ProjAB;
	float2 HF_Pad3;
};

//--------------------------------------------------------------------------------------
// Textures and Samplers
//--------------------------------------------------------------------------------------
SamplerState SS_Linear : register( s0 );
SamplerState SS_samMirror : register( s1 );
Texture2D	TX_Texture0 : register( t0 );
Texture2D	TX_Depth : register( t1 );

float3 VSPositionFromDepth(float depth, float2 vTexCoord)
{
	return ReconstructVSPositionFromDepthReverseZInfinite( depth, vTexCoord, HF_ProjParams.xy );
}

float ComputeVolumetricFog(float3 cameraToWorldPos, float3 posOriginal)
{	
	float cVolFogHeightDensityAtViewer = exp( -HF_HeightFalloff );
	
	float lenOrig = length(posOriginal - HF_CameraPosition);
	float len = length(cameraToWorldPos);
	float fogInt = len * cVolFogHeightDensityAtViewer;
	const float	cSlopeThreshold = 0.01;
	
	float w = saturate((lenOrig-HF_WeightZNear)/(HF_WeightZFar-HF_WeightZNear));

	if(abs( cameraToWorldPos.y ) > cSlopeThreshold )
	{
		float t = HF_HeightFalloff * cameraToWorldPos.y * w;
		fogInt *= (abs( t ) > 0.0001 ? ( ( 1.0 - exp( -t ) ) / t ) : 1.0);
	}
	
	
	
	return exp( -HF_GlobalDensity * w * fogInt );
}

float FogDither(float2 pixelPosition)
{
	float n1 = frac(52.9829189f * frac(dot(pixelPosition, float2(0.06711056f, 0.00583715f))));
	float n2 = frac(52.9829189f * frac(dot(pixelPosition + 37.17f, float2(0.00583715f, 0.06711056f))));
	return n1 + n2 - 1.0f;
}

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct PS_INPUT
{
	float2 vTexcoord		: TEXCOORD0;
	float3 vEyeRay			: TEXCOORD1;
	float4 vPosition		: SV_POSITION;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
	float expDepth = TX_Depth.Sample(SS_Linear, Input.vTexcoord).r;
	
	float3 position = VSPositionFromDepth(expDepth, Input.vTexcoord);
	
	
	position = mul(float4(position, 1), HF_InvView).xyz;
	float3 posOriginal = position;
	
	position -= HF_CameraPosition;
	
	position.y -= HF_FogHeight;
	
	float fog = 1.0f - ComputeVolumetricFog(position, posOriginal);
		
	float3 color = ApplyAtmosphericScatteringGround(position, HF_FogColorMod, true, false);
	float nightTimeBlend = smoothstep(0.0f, 1.0f, saturate(-AC_LightPos.y * 4.0f));
	nightTimeBlend *= saturate(AC_EnableNightAtmosphere);
	float nightFogBrightness = lerp(1.0f, max(0.0f, AC_NightFogBrightness), saturate(AC_EnableNightAtmosphere));
	float3 nightFogColor = float3(0.12f, 0.18f, 0.27f) * nightFogBrightness;
	color = lerp(color, nightFogColor, nightTimeBlend);

	// Restore the stable 17.9.7 blue daytime distance fog while keeping the
	// enhanced-night fog response unchanged.
	float dayDarknessFactor = max(1.0f, 2.0f - max(0.0f, AC_LightPos.y));
	float darknessFactor = lerp(dayDarknessFactor, 2.0f, nightTimeBlend);
	float maxFogOpacity = lerp(1.0f, 0.85f, nightTimeBlend);

	float3 ditheredFogColor = color / darknessFactor + FogDither(Input.vPosition.xy) * (1.5f / 255.0f);
	return float4(saturate(ditheredFogColor), saturate(fog) * maxFogOpacity);
}

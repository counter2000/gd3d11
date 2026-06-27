//--------------------------------------------------------------------------------------
// World/VOB-Pixelshader for G2D3D11 by Degenerated
//--------------------------------------------------------------------------------------
#include <DS_Defines.h>
#include "DepthReconstruction.h"
#include "include/PointLightShadows.h"

cbuffer DS_PointLightConstantBuffer : register( b0 )
{
	float4 PL_Color;
	
	float PL_Range;
	float3 Pl_PositionWorld;
	
	float PL_Outdoor;
	float3 Pl_PositionView;
	
	float2 PL_ViewportSize;
	float PL_IgnoreIndoorOutdoorLimit;
	float PL_ShadowSoftness;
	
	float4 PL_ProjParams; // x = 1/P._11, y = 1/P._22, z = P._43, w = P._33
	matrix PL_InvView; // Optimize out!
	
	float3 PL_LightScreenPos;
	float PL_ShadowStrength;
};

//--------------------------------------------------------------------------------------
// Textures and Samplers
//--------------------------------------------------------------------------------------
SamplerState SS_Linear : register( s0 );
SamplerState SS_samMirror : register( s1 );
SamplerComparisonState SS_Comp : register( s2 );
Texture2D	TX_Diffuse : register( t0 );
Texture2D	TX_Nrm : register( t1 );
Texture2D	TX_Depth : register( t2 );
TextureCube	TX_ShadowCube : register( t3 );
Texture2D	TX_SI_SP : register( t7 );

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct PS_INPUT
{
	float4 vPosition		: SV_POSITION;
};

float3 VSPositionFromDepth(float depth, float2 vTexCoord)
{
	return ReconstructVSPositionFromDepthReverseZInfinite( depth, vTexCoord, PL_ProjParams.xy ) * PL_ProjParams.z;
}

float DoorBleedDither(float2 pixelCoord, float salt)
{
	return frac(sin(dot(pixelCoord + salt, float2(12.9898f, 78.233f))) * 43758.5453f);
}
float ComputeIndoorDoorFloorBleed(float indoorPixel, float3 wsPosition, float3 wsNormal, float3 vsPosition, float3 lightPosWorld, float lightRange, float2 pixelCoord, float currentDepth)
{
	float outdoorPixel = 1.0f - indoorPixel;
	float floorMask = smoothstep(0.40f, 0.70f, wsNormal.y);
	float belowLight = smoothstep(-80.0f, 160.0f, lightPosWorld.y - wsPosition.y);
	float surfaceMask = lerp(0.35f, 1.0f, floorMask);
	float baseMask = outdoorPixel * surfaceMask * belowLight;
	if (baseMask <= 0.0f)
		return 0.0f;

	const float bleedWorldSize = 30.0f;
	float worldPixel = max(2.0f * abs(vsPosition.z) * max(PL_ProjParams.x / PL_ViewportSize.x, PL_ProjParams.y / PL_ViewportSize.y), 0.02f);
	int maxRadius = clamp((int)(bleedWorldSize / worldPixel + 0.5f), 1, 768);
	int2 baseCoord = clamp(int2(pixelCoord), int2(0, 0), int2(PL_ViewportSize) - int2(1, 1));
	float doorwayProbe = 0.0f;

	[unroll] for (int r = 0; r < 3; ++r)
	{
		float radiusJitter = DoorBleedDither(float2(pixelCoord), (float)r * 31.7f);
		int radius = max(1, (int)((maxRadius * ((float)r + 0.65f + radiusJitter * 0.7f)) / 3.0f + 0.5f));
		[unroll] for (int d = 0; d < 8; ++d)
		{
			int sx = (d == 0 || d == 4 || d == 5) ? radius : ((d == 1 || d == 6 || d == 7) ? -radius : 0);
			int sy = (d == 2 || d == 4 || d == 6) ? radius : ((d == 3 || d == 5 || d == 7) ? -radius : 0);
			int2 sampleCoord = clamp(baseCoord + int2(sx, sy), int2(0, 0), int2(PL_ViewportSize) - int2(1, 1));
			float4 sampleDiffuse = TX_Diffuse.Load(int3(sampleCoord, 0));
			float sampleIndoor = sampleDiffuse.a < 0.5f ? 1.0f : 0.0f;
			float sampleDepth = TX_Depth.Load(int3(sampleCoord, 0)).r;
			float2 sampleUV = (float2(sampleCoord) + 0.5f) / PL_ViewportSize;
			float3 sampleVS = VSPositionFromDepth(sampleDepth, sampleUV);
			float worldDistance = length(sampleVS - vsPosition);
			float worldFade = saturate(1.0f - worldDistance / bleedWorldSize);
			doorwayProbe = max(doorwayProbe, sampleIndoor * worldFade);
		}
	}

	return baseMask * doorwayProbe;
}
//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
	// Get screen UV
	float2 uv = Input.vPosition.xy / PL_ViewportSize;
	
	// Look up the diffuse color
	float4 diffuse = TX_Diffuse.Sample(SS_Linear, uv);
	
	// Get the second GBuffer
	float2 gb2 = TX_Nrm.Sample(SS_Linear, uv).xy;
	
	// Decode the view-space normal from octahedral R16G16_SNORM
	float3 normal = DecodeNormalGBuffer(gb2);
	
	// Get specular parameters
	float4 gb3 = TX_SI_SP.Sample(SS_Linear, uv);
	float specIntensity = gb3.x;
	float specPower = gb3.y < 0.0f ? max(-gb3.y - 1.0f, 1.0f) : gb3.y;
	
	// Reconstruct VS World Position from depth
	float expDepth = TX_Depth.Sample(SS_Linear, uv).r;
	float3 vsPosition = VSPositionFromDepth(expDepth, uv);
	float3 wsPosition = mul(float4(vsPosition, 1), PL_InvView).xyz;
	float3 wsNormal = normalize(mul(float4(normal, 0), PL_InvView).xyz);
	
	//return float4(normalize(wsPosition - Pl_PositionWorld), 1.0f);
	
	// Compute flat normal
	//float3 flatNormal = normalize(cross(ddx(vsPosition),ddy(vsPosition)));
	
	//if(Input.vPosition.z > expDepth)
	//	discard;
	
	//return float4(Pl_PositionView, 1);
	
	// Get direction and distance from the light to that position
	float3 lightDir = Pl_PositionView - vsPosition;
	float distance = length(lightDir);
	lightDir /= distance; // Normalize the direction
	
	// Do some simple NdL-Lighting
	float ndl = max(0, dot(lightDir, normal));
	
	// Apply dynamic shadow
	float shadow = PLS_SampleShadowCube(TX_ShadowCube, SS_Comp, wsPosition, wsNormal, Pl_PositionWorld, PL_Range, PL_ShadowSoftness);
	//return float4(ndl.rrr,1);
	
	// Get rid of lighting on the backfaces of normalmapped surfaces
	//ndl *= saturate(dot(lightDir, flatNormal)  / 0.00001f);
	
	// Compute range falloff
	float falloff = PLS_ComputeRangeFalloff(distance, PL_Range);
	
	// Compute specular lighting
	float3 V = normalize(-vsPosition);
	float3 H = normalize(lightDir + V);
	float spec = PLS_CalcBlinnPhongLighting(normal, H);
	float specMod = PLS_ComputeSpecMod(diffuse.rgb);
	float3 lighting = PLS_ComputePointLightLighting(diffuse.rgb, PL_Color.rgb, ndl, falloff, spec, specIntensity, specPower, specMod);
	
	lighting *= lerp(1.0f, shadow, saturate(PL_ShadowStrength));
	
	//lighting = GetShadow(uv);
	
	// Keep indoor point lights from leaking onto outdoor pixels, but allow a small
	// distance-stable doorway bleed so thresholds do not form a hard camera-dependent line.
	float indoor = 1.0f - PL_Outdoor;
	float indoorPixel = diffuse.a < 0.5f ? 1.0f : 0.0f;
	float doorFloorBleed = 0.0f;
	if (indoor > 0.5f && PL_IgnoreIndoorOutdoorLimit < 0.5f)
	{
		doorFloorBleed = ComputeIndoorDoorFloorBleed(indoorPixel, wsPosition, wsNormal, vsPosition, Pl_PositionWorld, PL_Range, Input.vPosition.xy, expDepth);
	}
	float indoorBoundary = saturate(PL_Outdoor + indoor * max(indoorPixel, doorFloorBleed));
	lighting *= lerp(indoorBoundary, 1.0f, saturate(PL_IgnoreIndoorOutdoorLimit));
	//return float4(0.2f,0.2f,0.2f,1);
	//return float4(ndl.rrr,1);
	return float4(saturate(lighting),1);
}

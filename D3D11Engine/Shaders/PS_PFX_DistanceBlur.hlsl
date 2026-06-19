//--------------------------------------------------------------------------------------
// World/VOB-Pixelshader for G2D3D11 by Degenerated
//--------------------------------------------------------------------------------------
#include <GaussBlur.h>

cbuffer B_BlurSettings : register(b0)
{
	float2 B_PixelSize;
	float B_BlurSize;
	float B_Threshold;

	float4 B_ColorMod;
	matrix B_InvProj;
};

//--------------------------------------------------------------------------------------
// Textures and Samplers
//--------------------------------------------------------------------------------------
SamplerState SS_Linear : register( s0 );
SamplerState SS_samMirror : register( s1 );
Texture2D	TX_Texture0 : register( t0 );
Texture2D	TX_Depth : register( t1 );

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct PS_INPUT
{
	float2 vTexcoord		: TEXCOORD0;
	float3 vEyeRay			: TEXCOORD1;
	float4 vPosition		: SV_POSITION;
};

float3 VSPositionFromDepth(float depth, float2 texCoord)
{
	float4 projectedPos = float4(texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f);
	float4 viewPos = mul(projectedPos, B_InvProj);
	return viewPos.xyz / viewPos.www;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
	float depth = TX_Depth.Sample(SS_Linear, Input.vTexcoord).r;
	float viewDistance = length(VSPositionFromDepth(depth, Input.vTexcoord));
	float blurMask = saturate(smoothstep(B_Threshold, B_ColorMod.y, viewDistance) * B_ColorMod.x);

	float2 ps = B_PixelSize * B_BlurSize * blurMask;
	float4 blur = DoBlurPassSingle(ps, Input.vTexcoord, TX_Texture0, TX_Depth, SS_Linear, 1.0f);
	
	float4 scene = TX_Texture0.Sample(SS_Linear, Input.vTexcoord);
	
	blur.a = 1.0f;
	scene.a = 1.0f;

	return lerp(scene, blur, blurMask);
}

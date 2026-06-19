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

float LoadDepthPoint(float2 uv, uint2 dimensions)
{
	uint2 pixel = min((uint2)(saturate(uv) * float2(dimensions)), dimensions - uint2(1, 1));
	return TX_Depth.Load(int3(pixel, 0)).r;
}

float4 DepthAwareBlur(float2 pixelStep, float2 texCoord, float centerDistance, uint2 depthDimensions)
{
	float4 colorSum = 0.0f;
	float weightSum = 0.0f;
	float depthTolerance = max(120.0f, centerDistance * 0.02f);
	float3 centerColor = TX_Texture0.Sample(SS_Linear, texCoord).rgb;

	[unroll]
	for (int x = -3; x <= 3; x++)
	{
		[unroll]
		for (int y = -3; y <= 3; y++)
		{
			float2 offset = float2(x, y);
			float2 uv = saturate(texCoord + offset * pixelStep);
			float sampleDepth = LoadDepthPoint(uv, depthDimensions);
			float sampleDistance = length(VSPositionFromDepth(sampleDepth, uv));
			float depthDelta = abs(sampleDistance - centerDistance);
			float depthWeight = 1.0f - smoothstep(depthTolerance, depthTolerance * 2.0f, depthDelta);
			float4 sampleColor = TX_Texture0.Sample(SS_Linear, uv);
			float colorDelta = length(sampleColor.rgb - centerColor);
			float silhouetteWeight = 1.0f - smoothstep(0.10f, 0.32f, colorDelta);
			float spatialWeight = rcp(1.0f + dot(offset, offset) * 0.22f);
			float weight = depthWeight * silhouetteWeight * spatialWeight;

			colorSum += sampleColor * weight;
			weightSum += weight;
		}
	}

	return colorSum / max(weightSum, 0.0001f);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
	uint depthWidth;
	uint depthHeight;
	TX_Depth.GetDimensions(depthWidth, depthHeight);
	uint2 depthDimensions = uint2(depthWidth, depthHeight);
	float depth = LoadDepthPoint(Input.vTexcoord, depthDimensions);
	float viewDistance = length(VSPositionFromDepth(depth, Input.vTexcoord));
	float blurMask = saturate(smoothstep(B_Threshold, B_ColorMod.y, viewDistance) * B_ColorMod.x);

	float2 ps = B_PixelSize * B_BlurSize * blurMask;
	float4 blur = DepthAwareBlur(ps, Input.vTexcoord, viewDistance, depthDimensions);
	
	float4 scene = TX_Texture0.Sample(SS_Linear, Input.vTexcoord);
	
	blur.a = 1.0f;
	scene.a = 1.0f;

	return lerp(scene, blur, blurMask);
}

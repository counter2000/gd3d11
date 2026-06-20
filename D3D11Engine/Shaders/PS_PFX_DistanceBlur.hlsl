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

static const int DISTANCE_BLUR_SAMPLE_COUNT = 48;

float2 GetSpiralSample(int index)
{
	float radius = sqrt((float(index) + 0.5f) / float(DISTANCE_BLUR_SAMPLE_COUNT));
	float angle = float(index) * 2.39996323f;
	return float2(radius * cos(angle), radius * sin(angle));
}

float4 DepthAwareBlur(float2 blurRadius, float2 texCoord, float centerDistance, uint2 depthDimensions)
{
	float4 centerColor = TX_Texture0.Sample(SS_Linear, texCoord);
	float4 colorSum = centerColor;
	float weightSum = 1.0f;
	float depthTolerance = max(120.0f, centerDistance * 0.02f);

	[unroll]
	for (int i = 0; i < DISTANCE_BLUR_SAMPLE_COUNT; i++)
	{
		float2 offset = GetSpiralSample(i);
		float2 uv = saturate(texCoord + offset * blurRadius);
		float sampleDepth = LoadDepthPoint(uv, depthDimensions);
		float sampleDistance = length(VSPositionFromDepth(sampleDepth, uv));
		float depthDelta = abs(sampleDistance - centerDistance);
		float depthWeight = 1.0f - smoothstep(depthTolerance, depthTolerance * 2.0f, depthDelta);

		float4 sampleColor = TX_Texture0.Sample(SS_Linear, uv);
		float colorDelta = length(sampleColor.rgb - centerColor.rgb);
		float silhouetteWeight = 1.0f - smoothstep(0.10f, 0.32f, colorDelta);
		silhouetteWeight = lerp(silhouetteWeight, 1.0f, saturate(B_ColorMod.z));
		float spatialWeight = exp(-dot(offset, offset) * 2.0f);
		float weight = depthWeight * silhouetteWeight * spatialWeight;

		colorSum += sampleColor * weight;
		weightSum += weight;
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

	// Erode the blur mask by one pixel at depth discontinuities. This keeps
	// thin foreground silhouettes such as leaves and fences sharp.
	float blurDistance = viewDistance;
	static const float2 neighborOffsets[4] = {
		float2(-1.0f, 0.0f), float2(1.0f, 0.0f),
		float2(0.0f, -1.0f), float2(0.0f, 1.0f)
	};
	[unroll]
	for (int i = 0; i < 4; i++)
	{
		float2 neighborUV = saturate(Input.vTexcoord + neighborOffsets[i] * B_PixelSize);
		float neighborDepth = LoadDepthPoint(neighborUV, depthDimensions);
		blurDistance = min(blurDistance, length(VSPositionFromDepth(neighborDepth, neighborUV)));
	}

	float dialogFocus = saturate(B_ColorMod.z);
	float blurStart = lerp(B_Threshold, 600.0f, dialogFocus);
	float blurEnd = lerp(B_ColorMod.y, 3200.0f, dialogFocus);
	float blurMask = saturate(smoothstep(blurStart, blurEnd, blurDistance) * B_ColorMod.x);

	float2 ps = B_PixelSize * B_BlurSize * blurMask * lerp(0.85f, 4.25f, dialogFocus);
	float4 blur = DepthAwareBlur(ps, Input.vTexcoord, viewDistance, depthDimensions);
	
	float4 scene = TX_Texture0.Sample(SS_Linear, Input.vTexcoord);
	
	blur.a = 1.0f;
	scene.a = 1.0f;

	return lerp(scene, blur, blurMask);
}

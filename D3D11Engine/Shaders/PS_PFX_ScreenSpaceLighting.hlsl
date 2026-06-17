//--------------------------------------------------------------------------------------
// Lightweight screen-space lighting refinements
//--------------------------------------------------------------------------------------

SamplerState SS_Linear : register( s0 );
Texture2D TX_Scene : register( t0 );
Texture2D TX_Depth : register( t1 );
Texture2D TX_Normal : register( t2 );

cbuffer ScreenSpaceEffects : register( b0 )
{
	float2 SSE_ViewportSize;
	float SSE_EnableContactShadows;
	float SSE_EnableSSGI;

	float SSE_ContactShadowStrength;
	float SSE_SSGIStrength;
	float2 SSE_Pad;
};

struct PS_INPUT
{
	float2 vTexcoord : TEXCOORD0;
	float3 vEyeRay : TEXCOORD1;
	float4 vPosition : SV_POSITION;
};

float Luminance(float3 color)
{
	return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float4 PSMain(PS_INPUT Input) : SV_TARGET
{
	float2 uv = Input.vTexcoord;
	float2 texel = 1.0f / max(SSE_ViewportSize, float2(1.0f, 1.0f));
	float4 scene = TX_Scene.Sample(SS_Linear, uv);
	float centerDepth = TX_Depth.Sample(SS_Linear, uv).r;
	float3 centerNormal = normalize(TX_Normal.Sample(SS_Linear, uv).xyz);

	float contact = 0.0f;
	float3 bounce = 0.0f;
	float bounceWeight = 0.0f;

	float2 offsets[8] = {
		float2(1.0f, 0.0f),
		float2(-1.0f, 0.0f),
		float2(0.0f, 1.0f),
		float2(0.0f, -1.0f),
		float2(1.0f, 1.0f),
		float2(-1.0f, 1.0f),
		float2(1.0f, -1.0f),
		float2(-1.0f, -1.0f)
	};

	[unroll]
	for (int i = 0; i < 8; i++) {
		float2 sampleUV = saturate(uv + offsets[i] * texel * 2.0f);
		float sampleDepth = TX_Depth.SampleLevel(SS_Linear, sampleUV, 0).r;
		float depthDelta = sampleDepth - centerDepth;

		contact += saturate(depthDelta * 300.0f - 0.02f);

		float3 sampleColor = TX_Scene.SampleLevel(SS_Linear, sampleUV, 0).rgb;
		float sameSurface = 1.0f - saturate(abs(depthDelta) * 120.0f);
		float3 sampleNormal = normalize(TX_Normal.SampleLevel(SS_Linear, sampleUV, 0).xyz);
		float normalWeight = saturate(dot(centerNormal, sampleNormal) * 0.5f + 0.5f);
		float weight = sameSurface * normalWeight;
		bounce += sampleColor * weight;
		bounceWeight += weight;
	}

	float3 color = scene.rgb;
	if (SSE_EnableContactShadows > 0.5f) {
		float shadow = saturate(contact / 8.0f) * saturate(SSE_ContactShadowStrength);
		color *= 1.0f - shadow * 0.45f;
	}

	if (SSE_EnableSSGI > 0.5f && bounceWeight > 0.001f) {
		bounce /= bounceWeight;
		float darkReceiver = 1.0f - saturate(Luminance(color) * 1.4f);
		color += bounce * darkReceiver * saturate(SSE_SSGIStrength) * 0.22f;
	}

	return float4(color, scene.a);
}

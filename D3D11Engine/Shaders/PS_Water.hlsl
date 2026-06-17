//--------------------------------------------------------------------------------------
// World/VOB-Pixelshader for G2D3D11 by Degenerated
//--------------------------------------------------------------------------------------
#include <AtmosphericScattering.h>
#include <FFFog.h>
#include <DS_Defines.h>

static const float DIST_SMALL_SPEED = -0.01f;
static const float DIST_SMALL_AMOUNT = 0.01f;
static const float DIST_SMALL_SCALE = 0.3f;
static const float DIST_BIG_SCALE = 0.1f;
static const float DIST_BIG_SPEED = -0.005f;


// Cleans the refraction borders
#define CleanRefraction(uv, screen_uv, depthRef) (lerp(uv, screen_uv, saturate(Input.vTexcoord2.x-depthRef)))

cbuffer RefractionInfo : register( b2 )
{
	float4x4 RI_Projection;
	float2 RI_ViewportSize;
	float RI_Time;
	float RI_Pad1;
	
	float3 RI_CameraPosition;
	float RI_Pad2;

	float4x4 RI_ViewProj;
};

cbuffer WaterReflectionInfo : register( b3 )
{
	float4 WR_LightPositionRange[8];
	float4 WR_LightColorIntensity[8];

	float WR_LightCount;
	float WR_EnableLightReflections;
	float WR_EnableShoreBlend;
	float WR_ShoreBlendStrength;

	float WR_LightReflectionStrength;
	float3 WR_Pad;
};

//--------------------------------------------------------------------------------------
// Textures and Samplers
//--------------------------------------------------------------------------------------
SamplerState SS_Linear : register( s0 );
SamplerState SS_samMirror : register( s1 );
Texture2D	TX_Diffuse : register( t0 );

Texture2D	TX_Depth : register( t2 );
TextureCube	TX_ReflectionCube : register( t3 );
Texture2D	TX_Distortion : register( t4 );
Texture2D	TX_Scene : register( t5 );

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct PS_INPUT
{
	float2 vTexcoord		: TEXCOORD0;
	float2 vTexcoord2		: TEXCOORD1;
	float4 vDiffuse			: TEXCOORD2;
	float3 vNormalWS		: TEXCOORD4;
	float3 vWorldPosition	: TEXCOORD5;
	float4 vPosition		: SV_POSITION;
};


//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
	float2 screenUV = Input.vPosition.xy / RI_ViewportSize;
	
	// Linear depth
	float depth = TX_Depth.Sample(SS_Linear, screenUV).r;
	depth = RI_Projection._43 / (depth - RI_Projection._33);
	
	// Clip here so we don't have to bind the depthbuffer
	//if(depth < Input.vTexcoord2.x) // vTexcoord2-x stores viewspace.z
	//	discard;
		
	float shallowDepth = saturate((depth - Input.vTexcoord2.x) * 0.01f);
		
	// Camera direction
	float3 viewDirection = normalize(Input.vWorldPosition - RI_CameraPosition);
		
	// Calculate distortion vectors
	float2 worldTexCoord = Input.vWorldPosition.xz / 1000.0f;
	float3 distortionSmall = TX_Distortion.Sample(SS_Linear, worldTexCoord * DIST_SMALL_SCALE + RI_Time * DIST_SMALL_SPEED).xyz * 2 - 1;
	distortionSmall += TX_Distortion.Sample(SS_Linear, worldTexCoord * float2(-1,0.7) * DIST_SMALL_SCALE + RI_Time * DIST_SMALL_SPEED * 2).xyz * 2 - 1;
	distortionSmall *= 0.5f;
	
	float3 distortionBig = TX_Distortion.Sample(SS_Linear, worldTexCoord * DIST_BIG_SCALE + RI_Time * DIST_BIG_SPEED).xyz * 2 - 1;
	distortionBig += TX_Distortion.Sample(SS_Linear, worldTexCoord * float2(-1,0.7) * DIST_BIG_SCALE + RI_Time * DIST_BIG_SPEED * 1.2).xyz * 2 - 1;
	distortionBig *= 0.5f;
	
	float2 distUV = screenUV + distortionSmall.xy * DIST_SMALL_AMOUNT + distortionBig.xy * DIST_SMALL_AMOUNT;
	
	// Distorted diffuse
	float3 diffuse = TX_Diffuse.Sample(SS_Linear, Input.vTexcoord + distortionSmall.xy * DIST_SMALL_AMOUNT * 0.5f).rgb;
	
	// Refracted depth
	float depthRefracted = TX_Depth.Sample(SS_Linear, distUV).r;
	depthRefracted = RI_Projection._43 / (depthRefracted - RI_Projection._33);
	
	distUV = CleanRefraction(distUV, screenUV, depthRefracted);
	distUV = saturate(distUV);
	
	// Wave vector
	float3 wavesDist = normalize(distortionSmall.xzy * float3(1,100,1));
	float3 wavesFres = normalize(distortionBig.xzy * float3(1,10,1));
	
	// Scene color
	float3 scene = TX_Scene.Sample(SS_Linear, distUV).rgb;
	float3 sceneClean = TX_Scene.Sample(SS_Linear, lerp(distUV, screenUV, pow(1-shallowDepth, 20.0f))).rgb;
	
	// Fresnel from waves
	float fresnel = min(0.5f, saturate(pow(1.0f - saturate(dot(-viewDirection, wavesFres)), 10.0f)));
	
	// Reflection
	float3 reflect_vec = reflect(-viewDirection, wavesFres);	
	
	// sample reflection cube
	float3 reflection = TX_ReflectionCube.Sample(SS_Linear, reflect_vec).xyz;
	float3 reflectionSSR = float3(0.0f, 0.0f, 0.0f);
	float ssrWeight = 0.0f;
	float ssrLuma = 0.0f;

	if (AC_EnableSSR > 0.5f) {
		float3 rayPos = Input.vWorldPosition;
		float3 rayDir = reflect(viewDirection, wavesFres);
		float stepSize = 40.0f;
		int maxSteps = 40;

		for (int i = 1; i <= maxSteps; i++) {
			rayPos += rayDir * stepSize;
			stepSize *= 1.1f;

			float4 projPos = mul(float4(rayPos, 1.0f), RI_ViewProj);
			projPos.xyz /= projPos.w;

			float2 uv = projPos.xy * float2(0.5f, -0.5f) + 0.5f;
			if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || projPos.z < 0.0f || projPos.z > 1.0f)
				break;

			float depthSample = TX_Depth.SampleLevel(SS_Linear, uv, 0).r;
			float sampleZ = RI_Projection._43 / (depthSample - RI_Projection._33);
			float rayZ = projPos.w;
			float depthDiff = rayZ - sampleZ;

			if (depthDiff > 0.0f && depthDiff < (stepSize * 2.0f)) {
				float3 minPos = rayPos - rayDir * stepSize;
				float3 maxPos = rayPos;
				float3 midPos = rayPos;

				[unroll]
				for (int j = 0; j < 5; j++) {
					midPos = (minPos + maxPos) * 0.5f;
					float4 projMid = mul(float4(midPos, 1.0f), RI_ViewProj);
					projMid.xyz /= projMid.w;
					float2 uvMid = projMid.xy * float2(0.5f, -0.5f) + 0.5f;
					float dMid = TX_Depth.SampleLevel(SS_Linear, uvMid, 0).r;
					float zMid = RI_Projection._43 / (dMid - RI_Projection._33);

					if (projMid.w - zMid > 0.0f) {
						maxPos = midPos;
					} else {
						minPos = midPos;
					}
				}

				float4 projFinal = mul(float4(midPos, 1.0f), RI_ViewProj);
				projFinal.xyz /= projFinal.w;
				uv = projFinal.xy * float2(0.5f, -0.5f) + 0.5f;

				reflectionSSR = TX_Scene.SampleLevel(SS_Linear, uv, 0).xyz;
				float2 edgeFade = saturate(abs(uv - 0.5f) * 2.0f);
				ssrWeight = saturate(pow(1.0f - max(edgeFade.x, edgeFade.y), 2.0f));
				ssrLuma = dot(reflectionSSR, float3(0.299f, 0.587f, 0.114f));
				break;
			}
		}
	}
	
	// Darken the scene, to make a wet surface
	float f = 1-saturate(pow(1-shallowDepth, 8.0f) + clamp(pow(distortionSmall.y, 2), 0.5f, 1.0f));

	float nightAmount = saturate((0.35f - AC_LightPos.y) * 1.2f);
	float3 sceneWet = lerp(sceneClean, sceneClean * lerp(0.01f, 0.65f, nightAmount), f); // Darken border-scene
	scene = lerp(scene, scene * float3(4, 0.2f, 0.1f) * 0.05f, f); // Darken distorted scene
	
	float pxDistance = Input.vTexcoord2.y;
	float ssrFresnel = lerp(0.20f, 0.75f, saturate(pow(1.0f - saturate(dot(-viewDirection, wavesFres)), 2.0f)));
	float3 waterTint = lerp(diffuse, float3(0.45f, 0.60f, 0.68f), 0.45f);
	float diffuseMix = lerp(0.73f, 0.48f, nightAmount);
	scene = lerp(scene, diffuse, diffuseMix * max(pow(fresnel,8.0f), 0.5f));
	float3 cubeReflection = reflection * lerp(float3(1.0f, 1.0f, 1.0f), waterTint, 0.25f);
	float cubeLuma = dot(cubeReflection, float3(0.299f, 0.587f, 0.114f));
	float daySSRWeight = ssrWeight * saturate((ssrLuma - 0.015f) * 8.0f);
	float nightSSRWeight = ssrWeight * saturate((ssrLuma - max(0.055f, cubeLuma * 0.55f)) * 10.0f);
	float ssrValidWeight = lerp(daySSRWeight, nightSSRWeight, nightAmount);
	scene.rgb += cubeReflection * (1.0f - ssrValidWeight) * fresnel * lerp(1.0f, diffuse, 0.6f);
	float3 reflectionSSRClamped = min(reflectionSSR, float3(0.95f, 1.00f, 1.05f));
	reflectionSSRClamped *= lerp(waterTint, float3(1.0f, 1.0f, 1.0f), 0.35f);
	scene.rgb += reflectionSSRClamped * ssrValidWeight * ssrFresnel * max(0.0f, AC_SSRStrength) * 0.82f;

	float3 waterLightReflection = 0.0f;
	if (WR_EnableLightReflections > 0.5f) {
		float3 reflectedView = normalize(reflect(-viewDirection, wavesFres));
		float nightWeight = lerp(0.45f, 1.25f, nightAmount);

		[unroll]
		for (int l = 0; l < 8; l++) {
			float activeLight = step((float)l + 0.5f, WR_LightCount);
			float3 lightPos = WR_LightPositionRange[l].xyz;
			float range = max(WR_LightPositionRange[l].w, 1.0f);
			float3 toLight = lightPos - Input.vWorldPosition;
			float distToLight = length(toLight);
			float attenuation = saturate(1.0f - distToLight / range);
			attenuation *= attenuation;

			float3 lightDir = toLight / max(distToLight, 0.001f);
			float specGlint = pow(saturate(dot(reflectedView, lightDir)), 52.0f) * attenuation;
			float planarDistance = length(toLight.xz);
			float surfaceGlow = saturate(1.0f - planarDistance / range);
			surfaceGlow *= surfaceGlow * saturate(1.0f - abs(toLight.y) / (range * 0.75f));

			float4 lightClip = mul(float4(lightPos, 1.0f), RI_ViewProj);
			float screenStreak = 0.0f;
			if (lightClip.w > 0.0f) {
				float2 lightUV = lightClip.xy / lightClip.w;
				lightUV = lightUV * float2(0.5f, -0.5f) + 0.5f;
				float2 delta = screenUV - lightUV;
				float belowLight = smoothstep(0.0f, 0.04f, delta.y);
				float verticalFalloff = saturate(1.0f - max(delta.y, 0.0f) * 1.7f);
				float horizontalFalloff = saturate(1.0f - abs(delta.x + distortionSmall.x * 0.025f) * 18.0f);
				screenStreak = belowLight * verticalFalloff * horizontalFalloff * attenuation;
			}

			float ripple = 0.70f + 0.30f * saturate(distortionSmall.y * 0.5f + 0.5f);
			waterLightReflection += WR_LightColorIntensity[l].rgb * (specGlint * 1.8f + screenStreak * 0.85f + surfaceGlow * 0.45f) * ripple * nightWeight * activeLight;
		}

		float3 screenLightReflection = 0.0f;
		[unroll]
		for (int s = 0; s < 12; s++) {
			float yOffset = 0.035f + (float)s * 0.035f;
			float xOffset = ((float)(s % 3) - 1.0f) * 0.020f + distortionSmall.x * 0.055f;
			float2 lightSampleUV = screenUV + float2(xOffset, -yOffset);
			float inScreen = step(0.0f, lightSampleUV.x) * step(lightSampleUV.x, 1.0f) * step(0.0f, lightSampleUV.y) * step(lightSampleUV.y, 1.0f);
			float3 lightSample = TX_Scene.SampleLevel(SS_Linear, saturate(lightSampleUV), 0).rgb;
			float lightMax = max(lightSample.r, max(lightSample.g, lightSample.b));
			float bright = saturate((lightMax - 0.08f) * 7.0f);
			float warmOrBright = saturate((lightSample.r - lightSample.b * 0.65f) * 3.5f + bright * 0.35f);
			float falloff = saturate(1.0f - (float)s / 12.0f);
			screenLightReflection += min(lightSample, float3(2.0f, 1.6f, 1.2f)) * bright * warmOrBright * falloff * inScreen;
		}
		waterLightReflection += screenLightReflection * nightWeight * saturate(1.0f - shallowDepth * 0.35f) * 0.55f;
	}

	float3 color = lerp(scene, sceneClean, pow(saturate(pxDistance / 35000.0f), 4.0f));
	color = lerp(color, sceneWet, (1-shallowDepth));

	if (WR_EnableShoreBlend > 0.5f) {
		float shore = 1.0f - shallowDepth;
		float shoreMask = smoothstep(0.02f, 0.85f, shore) * saturate(WR_ShoreBlendStrength);
		float shoreRipple = smoothstep(0.58f, 0.94f, shore) * (0.5f + 0.5f * saturate(distortionSmall.y * 0.5f + 0.5f));
		float3 shoreTint = lerp(sceneClean, color, 0.45f);
		color = lerp(color, shoreTint, shoreMask * lerp(0.32f, 0.48f, nightAmount));
		float foamNoise = saturate(distortionSmall.y * 0.45f + distortionBig.y * 0.35f + 0.55f);
		float foamBand = smoothstep(0.76f, 0.94f, shore) * (1.0f - smoothstep(0.97f, 1.0f, shore));
		float foamMask = shoreMask * foamBand * smoothstep(0.56f, 0.86f, foamNoise);
		color = lerp(color, sceneClean * 0.62f + float3(0.16f, 0.19f, 0.18f), foamMask * 0.16f);
		color += shoreRipple * shoreMask * float3(0.025f, 0.033f, 0.030f);
	}

	float transparencyLift = nightAmount * (1.0f - shallowDepth) * 0.72f;
	color = lerp(color, max(color, sceneClean * 0.82f + diffuse * 0.06f), transparencyLift);
	color.rgb += waterLightReflection * WR_LightReflectionStrength * ssrFresnel * saturate(shallowDepth + 0.45f);
	
	color.rgb = ApplyAtmosphericScatteringGround(Input.vWorldPosition, color.rgb);
	
	// Do spec lighting
	float3 sunOrange = float3(0.6,0.3,0.1) * 2.0f;
	float3 sunColor = lerp(sunOrange, 1.0f, AC_LightPos.y) * 5.0f;
	
	float3 reflect_vecSmall = reflect(-viewDirection, normalize(distortionSmall.xzy * float3(1,10,1)));
	
	float cos_spec = clamp(dot(reflect_vecSmall, -AC_LightPos.xyz * float3(1,1,1)), 0, 1);
	float sun_spot = pow(cos_spec, 500.0f) * 0.5f;
	color.rgb += lerp(sunColor * sun_spot, float3(0.0f, 0.0f, 0.0f), step(step(0.0f, AC_LightPos.y) * Input.vDiffuse.y, 0.5f));

	//darken / lighten water based on the day / night cycle
	float darknessFactor = lerp(2.0f - AC_LightPos.y, 1.18f, nightAmount * 0.65f);

	return float4(color / darknessFactor, 1);
}

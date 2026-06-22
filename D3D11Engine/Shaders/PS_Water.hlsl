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

float3 SampleWaterSSRReflection(float2 uv, float2 invResolution, float roughness)
{
	float2 spread = invResolution * lerp(1.5f, 5.0f, saturate(roughness));
	float3 color = TX_Scene.SampleLevel(SS_Linear, uv, 0).rgb * 0.44f;
	color += TX_Scene.SampleLevel(SS_Linear, saturate(uv + float2(spread.x, 0.0f)), 0).rgb * 0.14f;
	color += TX_Scene.SampleLevel(SS_Linear, saturate(uv - float2(spread.x, 0.0f)), 0).rgb * 0.14f;
	color += TX_Scene.SampleLevel(SS_Linear, saturate(uv + float2(0.0f, spread.y)), 0).rgb * 0.14f;
	color += TX_Scene.SampleLevel(SS_Linear, saturate(uv - float2(0.0f, spread.y)), 0).rgb * 0.14f;
	return color;
}

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
struct PS_OUTPUT
{
	float4 color : SV_TARGET0;
	float waterMask : SV_TARGET1;
};

PS_OUTPUT PSMain( PS_INPUT Input )
{
	PS_OUTPUT output;
	float2 screenUV = Input.vPosition.xy / RI_ViewportSize;
	
	// Linear depth
	float depth = TX_Depth.Sample(SS_Linear, screenUV).r;
	depth = RI_Projection._43 / (depth - RI_Projection._33);
	
	// Clip here so we don't have to bind the depthbuffer
	//if(depth < Input.vTexcoord2.x) // vTexcoord2-x stores viewspace.z
	//	discard;
		
	float shallowDepth = saturate((depth - Input.vTexcoord2.x) * 0.01f);
	float waterDepth = Input.vTexcoord2.x;
	float2 depthPixel = rcp(max(RI_ViewportSize, float2(1.0f, 1.0f)));
	float depthLeft = RI_Projection._43 / (TX_Depth.SampleLevel(SS_Linear, screenUV + float2(-depthPixel.x, 0.0f), 0).r - RI_Projection._33);
	float depthRight = RI_Projection._43 / (TX_Depth.SampleLevel(SS_Linear, screenUV + float2(depthPixel.x, 0.0f), 0).r - RI_Projection._33);
	float depthUp = RI_Projection._43 / (TX_Depth.SampleLevel(SS_Linear, screenUV + float2(0.0f, -depthPixel.y), 0).r - RI_Projection._33);
	float depthDown = RI_Projection._43 / (TX_Depth.SampleLevel(SS_Linear, screenUV + float2(0.0f, depthPixel.y), 0).r - RI_Projection._33);
	float nearestSceneDepth = min(min(depth, depthLeft), min(min(depthRight, depthUp), depthDown));
	float waterContactGap = nearestSceneDepth - waterDepth;
		
	// Camera direction
	float3 viewDirection = normalize(Input.vWorldPosition - RI_CameraPosition);
	float enhancedWater = step(0.5f, AC_EnableSSR);
		
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
	bool waterSSRActive = AC_EnableSSR > 0.5f && AC_SSRStrength > 0.001f;

	if (waterSSRActive) {
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

				float finalDepth = TX_Depth.SampleLevel(SS_Linear, uv, 0).r;
				if (finalDepth > 1e-7f) {
					float finalSceneZ = RI_Projection._43 / (finalDepth - RI_Projection._33);
					float2 hitPixel = rcp(max(RI_ViewportSize, float2(1.0f, 1.0f)));
					float zL = RI_Projection._43 / (TX_Depth.SampleLevel(SS_Linear, uv + float2(-hitPixel.x, 0.0f), 0).r - RI_Projection._33);
					float zR = RI_Projection._43 / (TX_Depth.SampleLevel(SS_Linear, uv + float2(hitPixel.x, 0.0f), 0).r - RI_Projection._33);
					float zU = RI_Projection._43 / (TX_Depth.SampleLevel(SS_Linear, uv + float2(0.0f, -hitPixel.y), 0).r - RI_Projection._33);
					float zD = RI_Projection._43 / (TX_Depth.SampleLevel(SS_Linear, uv + float2(0.0f, hitPixel.y), 0).r - RI_Projection._33);
					float depthEdge = max(max(abs(finalSceneZ - zL), abs(finalSceneZ - zR)), max(abs(finalSceneZ - zU), abs(finalSceneZ - zD)));
					float hitTolerance = clamp(abs(projFinal.w) * 0.0015f, 8.0f, 60.0f);
					float edgeTolerance = clamp(abs(finalSceneZ) * 0.012f, 18.0f, 120.0f);

					if (abs(projFinal.w - finalSceneZ) <= hitTolerance && depthEdge <= edgeTolerance) {
						float ssrRoughness = saturate(abs(Input.vTexcoord2.y) / 18000.0f + length(distortionSmall.xz) * 0.35f);
						reflectionSSR = SampleWaterSSRReflection(uv, rcp(max(RI_ViewportSize, float2(1.0f, 1.0f))), ssrRoughness);
						float2 edgeFade = saturate(abs(uv - 0.5f) * 2.0f);
						float edgeDistance = max(edgeFade.x, edgeFade.y);
						ssrWeight = 1.0f - smoothstep(0.78f, 1.0f, edgeDistance);
					}
				}
				break;
			}
		}
	}

	// Suppress unstable self-reflections at water/object contact edges (shoreline / wading NPCs).
	float ssrShallowFade = smoothstep(0.12f, 0.55f, shallowDepth);
	float ssrContactFade = smoothstep(35.0f, 180.0f, waterContactGap);
	float ssrNearFade = smoothstep(100.0f, 450.0f, abs(Input.vTexcoord2.y));
	ssrWeight *= ssrShallowFade * ssrContactFade * ssrNearFade;
	// Darken the scene, to make a wet surface
	float f = 1-saturate(pow(1-shallowDepth, 8.0f) + clamp(pow(distortionSmall.y, 2), 0.5f, 1.0f));
	float nightAmount = saturate((-AC_LightPos.y + 0.12f) * 2.2f);

	float3 sceneWet = lerp(sceneClean, sceneClean * lerp(0.01f, 0.38f, nightAmount * enhancedWater), f); // Darken border-scene
	scene = lerp(scene, scene * float3(4, 0.2f, 0.1f) * 0.05f, f); // Darken distorted scene
	
	float pxDistance = Input.vTexcoord2.y;
	scene = lerp(scene, diffuse, 0.73f * max(pow(fresnel,8.0f), 0.5f));
	float cubeWeight = (waterSSRActive ? lerp(0.45f, 0.95f, nightAmount) : 1.0f) * max(0.0f, AC_WaterCubemapStrength);
	scene.rgb += reflection * cubeWeight * (1.0f - ssrWeight * lerp(0.75f, 0.90f, nightAmount)) * fresnel * lerp(1.0f, diffuse, 0.6f);
	float ssrFresnel = lerp(0.55f, 0.80f, saturate(pow(1.0f - saturate(dot(-viewDirection, wavesFres)), 2.0f)));
	float3 reflectionSSRColor = max(reflectionSSR, float3(0.0f, 0.0f, 0.0f));
	float reflectionLuma = dot(reflectionSSRColor, float3(0.2126f, 0.7152f, 0.0722f));
	// Preserve HDR light-source reflections; only tame extreme outliers.
	reflectionSSRColor *= rcp(1.0f + max(0.0f, reflectionLuma - 6.0f) * 0.12f);
	float ssrBlend = saturate(ssrWeight * ssrFresnel * max(0.0f, AC_SSRStrength) * 0.78f * lerp(0.85f, 1.10f, nightAmount));
	float3 color = lerp(scene, sceneClean, pow(saturate(pxDistance / 35000.0f), 4.0f));
	color = lerp(color, sceneWet, (1-shallowDepth));

	color.rgb = ApplyAtmosphericScatteringGround(Input.vWorldPosition, color.rgb);
	
	// Do spec lighting
	float3 sunOrange = float3(0.6,0.3,0.1) * 2.0f;
	float3 sunColor = lerp(sunOrange, 1.0f, AC_LightPos.y) * 5.0f;
	
	float3 distortionFine = TX_Distortion.Sample(SS_Linear, worldTexCoord * 0.9f + RI_Time * -0.018f).xyz * 2 - 1;
	distortionFine += TX_Distortion.Sample(SS_Linear, worldTexCoord * float2(-1, 0.7f) * 1.25f + RI_Time * float2(0.014f, -0.019f)).xyz * 2 - 1;
	distortionFine *= 0.5f;
	float3 specNormal = normalize(lerp(distortionSmall.xzy, distortionFine.xzy, 0.65f) * float3(1,16,1));
	float3 reflect_vecSmall = reflect(-viewDirection, specNormal);
	
	float cos_spec = clamp(dot(reflect_vecSmall, -AC_LightPos.xyz * float3(1,1,1)), 0, 1);
	float sun_spot = pow(cos_spec, 820.0f) * 0.42f + pow(cos_spec, 180.0f) * 0.045f;
	color.rgb += lerp(sunColor * sun_spot, float3(0.0f, 0.0f, 0.0f), step(step(0.0f, AC_LightPos.y) * Input.vDiffuse.y, 0.5f));

	//darken / lighten water based on the day / night cycle
	float darknessFactor = 2.0f;
	darknessFactor -= AC_LightPos.y;
	darknessFactor = lerp(darknessFactor, max(1.22f, darknessFactor * 0.58f), nightAmount * enhancedWater);

	// TX_Scene already contains the fully lit and atmospherically shaded scene.
	// Blend SSR last so shallow-water coloring and water darkening cannot erase light reflections.
	float3 finalColor = color / darknessFactor;
	finalColor = lerp(finalColor, reflectionSSRColor, ssrBlend);
	output.color = float4(finalColor, 1);
	output.waterMask = 1.0f;
	return output;
}

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

    // --- Screen Space Reflections (SSR) ---
    if (AC_EnableSSR > 0.5f) {
        float3 rayPos = Input.vWorldPosition;
        float3 rayDir = normalize(reflect_vec);
        float stepSize = 40.0f;
        int maxSteps = 40;
        
        for (int i = 1; i <= maxSteps; i++) {
            rayPos += rayDir * stepSize;
            stepSize *= 1.1f; // Accelerate ray slightly
            
            // Project to screen space
            float4 projPos = mul(float4(rayPos, 1.0f), RI_ViewProj);
            projPos.xyz /= projPos.w;
            
            // Convert to UV
            float2 uv = projPos.xy * float2(0.5f, -0.5f) + 0.5f;
            
            // Out of bounds check
            if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || projPos.z < 0.0f || projPos.z > 1.0f)
                break;
                
            // Read scene depth at this UV
            float depthSample = TX_Depth.SampleLevel(SS_Linear, uv, 0).r;
            
            // Reconstruct view Z of sample
            float sampleZ = RI_Projection._43 / (depthSample - RI_Projection._33);
            float rayZ = RI_Projection._43 / (projPos.z - RI_Projection._33);
            
            float depthDiff = rayZ - sampleZ;
            
            // Intersection condition (behind surface, but not too far behind)
            if (depthDiff > 0.0f && depthDiff < 150.0f) {
                // Hit! Sample scene texture
                float3 ssrColor = TX_Scene.SampleLevel(SS_Linear, uv, 0).xyz;
                // Fade out near screen edges
                float2 edgeFade = saturate(abs(uv - 0.5f) * 2.0f);
                float fade = saturate(pow(1.0f - max(edgeFade.x, edgeFade.y), 2.0f));
                
                reflection = lerp(reflection, ssrColor, fade);
                break;
            }
        }
    }
    // --- End SSR ---
	
	// Beer-Lambert Law for Depth Absorption
	float waterDepth = max(0, depthRefracted - Input.vTexcoord2.x);
	float3 absorptionCoef = float3(0.005f, 0.001f, 0.0005f); // Red absorbed fast, green medium, blue slow
	float3 transmittance = exp(-waterDepth * absorptionCoef);
	float3 deepWaterColor = float3(0.02f, 0.12f, 0.15f); // Dark greenish blue for deep water

	float3 sceneAbsorbed = sceneClean * transmittance + deepWaterColor * (1.0f - transmittance);
	
	// Waterfalls (vertical normals) should not use depth absorption, because the depth buffer 
	// measures the distance to the background, not the thickness of the falling water.
	float isWaterfall = saturate(pow(1.0f - abs(Input.vNormalWS.y), 4.0f));
	
	scene = lerp(sceneAbsorbed, scene, isWaterfall);
	
	float pxDistance = Input.vTexcoord2.y;
	scene = lerp(scene, diffuse, 0.73f * max(pow(fresnel,8.0f), 0.5f));
	scene.rgb += reflection * 1.0f * fresnel * lerp(1.0f, diffuse, 0.6f);
	float3 color = lerp(scene, sceneClean, pow(saturate(pxDistance / 35000.0f), 4.0f));
	
	color.rgb = ApplyAtmosphericScatteringGround(Input.vWorldPosition, color.rgb);
	
	// Do spec lighting
	float3 sunOrange = float3(0.6,0.3,0.1) * 2.0f;
	float3 sunColor = lerp(sunOrange, 1.0f, AC_LightPos.y) * 5.0f;
	
	float3 reflect_vecSmall = reflect(-viewDirection, normalize(distortionSmall.xzy * float3(1,10,1)));
	
	float cos_spec = clamp(dot(reflect_vecSmall, -AC_LightPos.xyz * float3(1,1,1)), 0, 1);
	float sun_spot = pow(cos_spec, 500.0f) * 0.5f;
	color.rgb += lerp(sunColor * sun_spot, float3(0.0f, 0.0f, 0.0f), step(step(0.0f, AC_LightPos.y) * Input.vDiffuse.y, 0.5f));

	//darken / lighten water based on the day / night cycle
	float darknessFactor = 2.0f;
	darknessFactor -= AC_LightPos.y;

	return float4(color / darknessFactor, 1);
}



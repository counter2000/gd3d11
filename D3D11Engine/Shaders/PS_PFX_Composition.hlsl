//--------------------------------------------------------------------------------------
// PostFX Composition Uber Shader
// Merges SAO, HeightFog, and GodRays into a single full-screen pass.
// Permutation macros: COMPOSE_SAO, COMPOSE_HEIGHTFOG, COMPOSE_GODRAYS
//--------------------------------------------------------------------------------------

#if COMPOSE_HEIGHTFOG || COMPOSE_LIGHTSHAFTS || COMPOSE_CONTACT_SHADOWS || COMPOSE_SSGI
#include <AtmosphericScattering.h>
#endif
#if COMPOSE_HEIGHTFOG
#include "DepthReconstruction.h"
#endif

//--------------------------------------------------------------------------------------
// Constant Buffers
//--------------------------------------------------------------------------------------
#if COMPOSE_HEIGHTFOG
cbuffer PFXBuffer : register( b0 )
{
    float4 HF_ProjParams;
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
#endif

//--------------------------------------------------------------------------------------
// Textures and Samplers
//--------------------------------------------------------------------------------------
SamplerState SS_Linear : register( s0 );

Texture2D TX_Backbuffer : register( t0 );

#if COMPOSE_SAO
Texture2D TX_SAO : register( t1 );
#endif

#if COMPOSE_GODRAYS
Texture2D TX_GodRays : register( t2 );
#endif

#if COMPOSE_HEIGHTFOG || COMPOSE_LIGHTSHAFTS || COMPOSE_CONTACT_SHADOWS || COMPOSE_SSGI
Texture2D TX_Depth : register( t3 );
#endif

//--------------------------------------------------------------------------------------
// HeightFog helpers (inlined from PS_PFX_Heightfog.hlsl)
//--------------------------------------------------------------------------------------
#if COMPOSE_HEIGHTFOG
float3 VSPositionFromDepth( float depth, float2 vTexCoord )
{
    return ReconstructVSPositionFromDepthReverseZInfinite( depth, vTexCoord, HF_ProjParams.xy );
}

float ComputeVolumetricFog( float3 cameraToWorldPos, float3 posOriginal )
{
    float cVolFogHeightDensityAtViewer = exp( -HF_HeightFalloff );

    float lenOrig = length( posOriginal - HF_CameraPosition );
    float len = length( cameraToWorldPos );
    float fogInt = len * cVolFogHeightDensityAtViewer;
    const float cSlopeThreshold = 0.01;

    float w = saturate( ( lenOrig - HF_WeightZNear ) / ( HF_WeightZFar - HF_WeightZNear ) );

    if ( abs( cameraToWorldPos.y ) > cSlopeThreshold )
    {
        float t = HF_HeightFalloff * cameraToWorldPos.y * w;
        fogInt *= ( abs( t ) > 0.0001 ? ( ( 1.0 - exp( -t ) ) / t ) : 1.0 );
    }

    return exp( -HF_GlobalDensity * w * fogInt );
}

float FogDither(float2 pixelPosition)
{
    float n1 = frac(52.9829189f * frac(dot(pixelPosition, float2(0.06711056f, 0.00583715f))));
    float n2 = frac(52.9829189f * frac(dot(pixelPosition + 37.17f, float2(0.00583715f, 0.06711056f))));
    return n1 + n2 - 1.0f;
}

float4 ComputeHeightFog( float2 texcoord, float2 pixelPosition )
{
    float expDepth = TX_Depth.Sample( SS_Linear, texcoord ).r;
    float3 position = VSPositionFromDepth( expDepth, texcoord );
    position = mul( float4( position, 1 ), HF_InvView ).xyz;
    float3 posOriginal = position;
    position -= HF_CameraPosition;
    position.y -= HF_FogHeight;

    float fog = 1.0f - ComputeVolumetricFog( position, posOriginal );
    float fogDistance = length(posOriginal - HF_CameraPosition);
    float stableFadeEnd = max(HF_WeightZFar, 1000.0f);
    float stableFadeStart = max(HF_WeightZNear, stableFadeEnd * 0.82f);
    float stableWorldFade = smoothstep(stableFadeStart, stableFadeEnd, fogDistance);
    float activeWeatherFog = saturate(AC_RainFXWeight);
	float nightTimeBlend = smoothstep(0.0f, 1.0f, saturate(-AC_LightPos.y * 4.0f))
		* saturate(AC_EnableNightAtmosphere);
    float weatherFog = max(fog, stableWorldFade) * activeWeatherFog;
    float dryNightFog = fog * nightTimeBlend * (1.0f - activeWeatherFog);
    fog = max(weatherFog, dryNightFog);
    float fogGradientWeight = saturate(fog * (1.0f - fog) * 4.0f);
    float fogGradientDither = FogDither(pixelPosition) * nightTimeBlend * (4.0f / 255.0f);
    float ditheredFog = saturate(fog + fogGradientDither * fogGradientWeight);
    float3 color = ApplyAtmosphericScatteringGround( position, HF_FogColorMod, true, false );
	float nightFogBrightness = lerp(1.0f, max(0.0f, AC_NightFogBrightness), saturate(AC_EnableNightAtmosphere));
	float3 nightFogColor = float3(0.12f, 0.18f, 0.27f) * nightFogBrightness;
	color = lerp(color, nightFogColor, nightTimeBlend);

	float dayDarknessFactor = max(1.0f, 2.0f - max(0.0f, AC_LightPos.y));
	float darknessFactor = lerp(dayDarknessFactor, 2.5f, nightTimeBlend);
	float maxFogOpacity = lerp(1.0f, 0.85f, nightTimeBlend);

	return float4(saturate(color / darknessFactor), ditheredFog * maxFogOpacity);
}
#endif

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct PS_INPUT
{
    float2 vTexcoord  : TEXCOORD0;
    float3 vEyeRay    : TEXCOORD1;
    float4 vPosition  : SV_POSITION;
};


#if COMPOSE_LIGHTSHAFTS || COMPOSE_CONTACT_SHADOWS || COMPOSE_SSGI
float GetDepthRaw(float2 uv)
{
    return TX_Depth.SampleLevel(SS_Linear, saturate(uv), 0).r;
}

float IsGeometryPixel(float depth)
{
    return step(0.000001f, depth);
}

float2 GetScreenPixelSize(float2 uv)
{
    return max(abs(ddx(uv)) + abs(ddy(uv)), float2(1.0f / 3840.0f, 1.0f / 2160.0f));
}

float DepthSimilarity(float centerDepth, float sampleDepth, float nearScale, float farScale)
{
    float depthScale = max(centerDepth, 0.00008f);
    float delta = abs(sampleDepth - centerDepth);
    float nearLimit = max(depthScale * nearScale, 0.000015f);
    float farLimit = max(depthScale * farScale, 0.00018f);
    return (1.0f - smoothstep(nearLimit, farLimit, delta)) * IsGeometryPixel(sampleDepth);
}
#endif

#if COMPOSE_CONTACT_SHADOWS
float ComputeContactShadow(float2 uv, float centerDepth)
{
    float geom = IsGeometryPixel(centerDepth);
    float2 px = GetScreenPixelSize(uv);

    float2 projectedLight = AC_LightScreenPos.xy - uv;
    float projectedLen = length(projectedLight);
    float2 fallbackDir = normalize(float2(AC_LightPos.x, -AC_LightPos.z) + float2(0.001f, -0.001f));
    float2 lightDir = projectedLen > 0.03f ? projectedLight / projectedLen : fallbackDir;

    // Suppress silhouette outlines: contact shadows should live on locally coherent surfaces.
    float localSurface = 0.0f;
    localSurface += DepthSimilarity(centerDepth, GetDepthRaw(uv + float2( px.x, 0.0f)), 0.04f, 0.22f);
    localSurface += DepthSimilarity(centerDepth, GetDepthRaw(uv + float2(-px.x, 0.0f)), 0.04f, 0.22f);
    localSurface += DepthSimilarity(centerDepth, GetDepthRaw(uv + float2(0.0f,  px.y)), 0.04f, 0.22f);
    localSurface += DepthSimilarity(centerDepth, GetDepthRaw(uv + float2(0.0f, -px.y)), 0.04f, 0.22f);
    localSurface = saturate(localSurface * 0.25f);

    float shadow = 0.0f;
    float weightSum = 0.0001f;
    [unroll]
    for (int i = 1; i <= 7; ++i)
    {
        float fi = (float)i;
        float2 suv = uv + lightDir * px * (fi * 2.25f + fi * fi * 0.35f);
        float sd = GetDepthRaw(suv);

        // Reverse-Z: a larger sampled depth is closer to the camera and can occlude this pixel.
        float delta = sd - centerDepth;
        float depthScale = max(centerDepth, 0.00008f);
        float minDelta = max(depthScale * 0.025f, 0.000018f);
        float maxDelta = max(depthScale * 0.28f, 0.00032f);
        float occluder = smoothstep(minDelta, maxDelta, delta) * (1.0f - smoothstep(maxDelta, maxDelta * 3.5f, delta));
        occluder *= IsGeometryPixel(sd);

        float weight = 1.0f - fi * 0.09f;
        shadow += occluder * weight;
        weightSum += weight;
    }

    shadow = saturate(shadow / weightSum) * localSurface;
    float lightVisibility = saturate(AC_LightScreenPos.z + 0.65f);
    float strength = saturate(AC_ContactShadowStrength);
    return 1.0f - shadow * geom * lightVisibility * strength * 0.62f;
}
#endif

#if COMPOSE_SSGI
float3 ComputeScreenSpaceGILight(float2 uv, float centerDepth, float3 baseColor)
{
    float geom = IsGeometryPixel(centerDepth);
    float2 px = GetScreenPixelSize(uv);
    float3 bounce = 0.0f;
    float weightSum = 0.0001f;
    const float2 dirs[8] = {
        float2( 1.0f,  0.0f), float2(-1.0f,  0.0f),
        float2( 0.0f,  1.0f), float2( 0.0f, -1.0f),
        float2( 0.707f,  0.707f), float2(-0.707f, -0.707f),
        float2( 0.707f, -0.707f), float2(-0.707f,  0.707f)
    };

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float2 suvNear = saturate(uv + dirs[i] * px * 6.0f);
        float2 suvFar = saturate(uv + dirs[i] * px * 14.0f);
        float sd0 = GetDepthRaw(suvNear);
        float sd1 = GetDepthRaw(suvFar);
        float w0 = DepthSimilarity(centerDepth, sd0, 0.10f, 0.75f);
        float w1 = DepthSimilarity(centerDepth, sd1, 0.18f, 1.15f) * 0.55f;
        bounce += TX_Backbuffer.SampleLevel(SS_Linear, suvNear, 0).rgb * w0;
        bounce += TX_Backbuffer.SampleLevel(SS_Linear, suvFar, 0).rgb * w1;
        weightSum += w0 + w1;
    }

    bounce /= weightSum;

    // This is intentionally a conservative screen-space indirect light/bleed, not fake bloom-only.
    float3 chromaBleed = max(bounce - baseColor * 0.18f, 0.0f);
    float3 softLift = bounce * saturate(1.0f - dot(baseColor, float3(0.2126f, 0.7152f, 0.0722f))) * 0.10f;
    return (chromaBleed * 0.72f + softLift) * saturate(AC_ScreenSpaceGIStrength) * geom * 0.72f;
}
#endif

#if COMPOSE_LIGHTSHAFTS
float3 ComputeVolumetricLightShafts(float2 uv, float depth)
{
    float day = saturate(AC_LightPos.y * 2.0f + 0.15f);
    float night = saturate((-AC_LightPos.y + 0.05f) * 1.5f);
    float weather = max(saturate(AC_RainFXWeight), saturate(AC_SceneWettness));
    float atmosphereWeight = saturate(day + night * 0.28f) * lerp(1.0f, 0.28f, weather);

    float2 lightCenter = AC_LightScreenPos.xy;
    float2 toLight = lightCenter - uv;
    float distToLight = length(toLight);
    float2 dir = toLight / max(distToLight, 0.0001f);

    float visibility = 0.0f;
    float visibilityWeight = 0.0f;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        float t = ((float)i + 0.5f) / 16.0f;
        float2 suv = uv + dir * t * 0.24f;
        float inBounds = step(0.0f, suv.x) * step(suv.x, 1.0f) * step(0.0f, suv.y) * step(suv.y, 1.0f);
        if (inBounds > 0.0f)
        {
            float sd = GetDepthRaw(suv);
            float sky = 1.0f - IsGeometryPixel(sd);
            float sampleWeight = (1.0f - t * 0.10f);
            visibility += lerp(0.38f, 1.0f, sky) * sampleWeight;
            visibilityWeight += sampleWeight;
        }
    }
    visibility /= max(visibilityWeight, 0.001f);

    float geom = IsGeometryPixel(depth);
    float geometryMist = lerp(0.82f, 1.0f, geom);
    float radial = pow(1.0f - smoothstep(0.00f, 1.34f, distToLight), 1.12f);
    float skyShaftLimit = lerp(0.42f, 1.0f, geom);
    float onScreenLight = saturate(AC_LightScreenPos.z + 0.18f);
    float shaft = visibility * geometryMist * atmosphereWeight * radial * skyShaftLimit * onScreenLight;

    float3 dayColor = float3(1.0f, 0.80f, 0.48f);
    float3 nightColor = float3(0.18f, 0.28f, 0.55f);
    float3 rainColor = float3(0.36f, 0.46f, 0.58f);
    float3 shaftColor = lerp(lerp(dayColor, nightColor, night), rainColor, weather * 0.35f);
    return shaftColor * shaft * saturate(AC_VolumetricLightShaftStrength) * lerp(0.46f, 0.18f, weather);
}
#endif
//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
    float4 color = TX_Backbuffer.Sample( SS_Linear, Input.vTexcoord );

    // Composition order: SAO (multiply) -> HeightFog (alpha blend) -> GodRays (additive)

#if COMPOSE_SAO
    float ao = TX_SAO.Sample( SS_Linear, Input.vTexcoord ).r;
    color.rgb *= ao;
#endif

#if COMPOSE_CONTACT_SHADOWS || COMPOSE_SSGI || COMPOSE_LIGHTSHAFTS
    float compositionDepth = GetDepthRaw(Input.vTexcoord);
#endif

#if COMPOSE_CONTACT_SHADOWS
    color.rgb *= ComputeContactShadow(Input.vTexcoord, compositionDepth);
#endif

#if COMPOSE_SSGI
    color.rgb += ComputeScreenSpaceGILight(Input.vTexcoord, compositionDepth, color.rgb);
#endif

#if COMPOSE_HEIGHTFOG
    float4 fog = ComputeHeightFog( Input.vTexcoord, Input.vPosition.xy );
    color.rgb = lerp( color.rgb, fog.rgb, fog.a );
    float nightTimeBlend = smoothstep(0.0f, 1.0f, saturate(-AC_LightPos.y * 4.0f));
    float ditherStrength = lerp(2.0f, 5.0f, nightTimeBlend) / 255.0f;
    color.rgb = saturate(color.rgb + FogDither(Input.vPosition.xy) * fog.a * ditherStrength);
#endif

#if COMPOSE_GODRAYS
    float3 godrays = TX_GodRays.Sample( SS_Linear, Input.vTexcoord ).rgb;
    color.rgb += godrays;
#endif

#if COMPOSE_LIGHTSHAFTS
    float3 lightShafts = ComputeVolumetricLightShafts(Input.vTexcoord, compositionDepth);
    color.rgb += lightShafts;
#endif

    return color;
}

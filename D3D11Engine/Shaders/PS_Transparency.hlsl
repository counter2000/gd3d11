//--------------------------------------------------------------------------------------
// Ghost NPC Buffer
//--------------------------------------------------------------------------------------
cbuffer GhostAlphaInfo : register( b0 )
{
	float2 GA_ViewportSize;
    float GA_Alpha;
    float GA_LightingScale;
};

cbuffer DecalSoftParticleInfo : register( b1 )
{
    float4 DSP_DepthParams; // x=projection._33, y=projection._43, z=fade distance
};

//--------------------------------------------------------------------------------------
// Textures and Samplers
//--------------------------------------------------------------------------------------
SamplerState SS_Linear : register( s0 );
Texture2D	TX_Texture0 : register( t0 );
Texture2D	TX_Depth : register( t3 );
Texture2D	TX_Scene : register( t5 );

//--------------------------------------------------------------------------------------
// Input / Output structures
//--------------------------------------------------------------------------------------
struct PS_INPUT
{
	float2 vTexcoord		: TEXCOORD0;
	float2 vTexcoord2		: TEXCOORD1;
	float4 vDiffuse			: TEXCOORD2;
	float3 vNormalVS		: TEXCOORD4;
	float3 vViewPosition	: TEXCOORD5;
	float4 vPosition		: SV_POSITION;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PSMain( PS_INPUT Input ) : SV_TARGET
{
	//float2 screenUV = Input.vPosition.xy / GA_ViewportSize;
	//float3 screenColor = TX_Scene.Sample(SS_Linear, screenUV).rgb;
	//float screenLuma = 0.2126 * screenColor.r + 0.7125 * screenColor.g + 0.0722 * screenColor.b;

	float4 color = TX_Texture0.Sample(SS_Linear, Input.vTexcoord);
    float lightingScale = abs(GA_LightingScale);
    float softFade = 1.0f;
    if (GA_LightingScale < 0.0f && Input.vViewPosition.z > 0.0f)
    {
        float2 screenUV = Input.vPosition.xy / GA_ViewportSize;
        float sceneDepth = TX_Depth.Sample(SS_Linear, saturate(screenUV)).r;
        if (sceneDepth > 1e-7f)
        {
            float sceneViewDepth = DSP_DepthParams.y / (sceneDepth - DSP_DepthParams.x);
            float depthDifference = sceneViewDepth - Input.vViewPosition.z;
            softFade = depthDifference <= 0.0f
                ? 0.0f
                : smoothstep(0.0f, 1.0f, saturate(depthDifference / DSP_DepthParams.z));
        }
    }
    // Negative lighting scale marks emissive additive decals; magnitude remains the color scale.
    return float4(color.rgb * lightingScale * softFade, color.a * GA_Alpha * softFade);
}

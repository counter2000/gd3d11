#define TRI_SAMPLE_LEVEL 0
#include <Triplanar.h>

cbuffer Matrices_PerFrame : register( b0 )
{
	matrix M_View;
	matrix M_Proj;
	matrix M_ViewProj;	
};

cbuffer OceanSettings : register( b1 )
{
	float3 OS_CameraPosition;
	float OS_SpecularPower;
	
	// Water-reflected sky color
	float3			OS_SkyColor;
	float			unused0;
	// The color of bottomless water body
	float3			OS_WaterbodyColor;

	// The strength, direction and color of sun streak
	float			OS_Shineness;
	float3			OS_SunDir;
	float			unused1;
	float3			OS_SunColor;
	float			unused2;
	
	// The parameter is used for fixing an artifact
	float3			OS_BendParam;

	// Perlin noise for distant wave crest
	float			OS_PerlinSize;
	float3			OS_PerlinAmplitude;
	float			unused3;
	float3			OS_PerlinOctave;
	float			unused4;
	float3			OS_PerlinGradient;

	// Constants for calculating texcoord from position
	float			OS_TexelLength_x2;
	float			OS_UVScale;
	float			OS_UVOffset;
}

struct VS_OUTPUT
{
	float2 vTexcoord		: TEXCOORD0;
	float2 vTexcoord2		: TEXCOORD1;
	float4 vDiffuse			: TEXCOORD2;
	float3 vNormalWS		: TEXCOORD4;
	float3 vWorldPosition	: TEXCOORD5;
	float4 vPosition		: SV_POSITION;
};

struct PS_INPUT
{
	float2 vTexcoord		: TEXCOORD0;
	float2 vTexcoord2		: TEXCOORD1;
	float4 vDiffuse			: TEXCOORD2;
	float3 vNormalWS		: TEXCOORD4;
	float3 vWorldPosition	: TEXCOORD5;
	float4 vPosition		: SV_POSITION;
};

struct ConstantOutputType
{
    float edges[3] : SV_TessFactor;
    float inside: SV_InsideTessFactor;
};

Texture2D TX_Texture0 : register( t0 );  
SamplerState SS_Linear : register( s0 );

float3 CalculateGerstnerWave(float2 dir, float steepness, float wavelength, float3 p, float time) {
    float k = 2 * 3.14159 / wavelength;
    float c = sqrt(9.8 / k);
    float2 d = normalize(dir);
    float f = k * (dot(d, p.xz) - c * time);
    float a = steepness / k;
    return float3(d.x * (a * cos(f)), a * sin(f), d.y * (a * cos(f)));
}

struct HS_ConstantOutput 
{ 
    float fTessFactor[3]        : SV_TessFactor; 
    float fInsideTessFactor[1]  : SV_InsideTessFactor; 
}; 

HS_ConstantOutput HS_Constant( InputPatch<VS_OUTPUT, 3> I ) 
{ 
    HS_ConstantOutput O = (HS_ConstantOutput)0; 
	
	// Distance-based tessellation factor
	float3 avgPos = (I[0].vWorldPosition + I[1].vWorldPosition + I[2].vWorldPosition) / 3.0f;
	float distance = length(avgPos - OS_CameraPosition);
	float tessFactor = clamp(6400.0f / max(distance, 1.0f), 1.0f, 16.0f);
	
    O.fTessFactor[0] = tessFactor; 
    O.fTessFactor[1] = tessFactor; 
    O.fTessFactor[2] = tessFactor; 
    O.fInsideTessFactor[0] = tessFactor; 
    return O; 
} 

[domain("tri")] 
[partitioning("fractional_odd")] 
[outputtopology("triangle_cw")] 
[patchconstantfunc("HS_Constant")] 
[outputcontrolpoints(3)] 
VS_OUTPUT HSMain( InputPatch<VS_OUTPUT, 3> I, uint uCPID : SV_OutputControlPointID ) 
{ 
	return I[uCPID];
}

[domain("tri")]
PS_INPUT DSMain(HS_ConstantOutput input, float3 uvwCoord : SV_DomainLocation, const OutputPatch<VS_OUTPUT, 3> patch)
{
    float4 vertexPosition;
    float2 texCoord;
	float2 texCoord2;
	float3 normalWS;
	float4 diffuse;
    PS_INPUT output;
    float3 viewPosition;
	float3 worldPosition;
  
    diffuse = uvwCoord.x * patch[0].vDiffuse + uvwCoord.y * patch[1].vDiffuse + uvwCoord.z * patch[2].vDiffuse;
	worldPosition = uvwCoord.x * patch[0].vWorldPosition + uvwCoord.y * patch[1].vWorldPosition + uvwCoord.z * patch[2].vWorldPosition;
    texCoord = uvwCoord.x * patch[0].vTexcoord + uvwCoord.y * patch[1].vTexcoord + uvwCoord.z * patch[2].vTexcoord;
	texCoord2 = uvwCoord.x * patch[0].vTexcoord2 + uvwCoord.y * patch[1].vTexcoord2 + uvwCoord.z * patch[2].vTexcoord2;
	normalWS = uvwCoord.x * patch[0].vNormalWS + uvwCoord.y * patch[1].vNormalWS + uvwCoord.z * patch[2].vNormalWS;
	
	// Use OS_UVOffset as a time variable
	float time = OS_UVOffset * 50.0f; 
	
	// Gerstner Waves
	float3 waveDisp = CalculateGerstnerWave(float2(1.0, 0.7), 0.35, 1000.0, worldPosition, time);
	waveDisp += CalculateGerstnerWave(float2(0.8, 1.0), 0.25, 500.0, worldPosition, time * 1.2);
	waveDisp += CalculateGerstnerWave(float2(-0.2, 1.0), 0.15, 250.0, worldPosition, time * 1.5);

	float3 displacement = waveDisp * 0.5f;
						
	float3 vHeight = 1.0f * displacement;
	worldPosition += vHeight;
	
   /* vertexPosition.xyz += normalVS * (vHeight);
	
    output.vPosition = vertexPosition;
	output.vTexcoord2 = texCoords;
	output.vTexcoord = texCoords;
	output.vDiffuse  = diffuse;
	
	output.vWorldPosition = worldPosition;
	output.vViewPosition = viewPosition;
	
	output.vNormalWS = normalWS;
	output.vNormalVS = normalVS;*/
		
	output.vPosition = mul( float4(worldPosition,1), M_ViewProj);
	output.vTexcoord2 = texCoord2;
	output.vTexcoord = texCoord;
	output.vDiffuse  = diffuse;
	output.vNormalWS = normalWS;
	output.vWorldPosition = worldPosition;
	
    return output;
}
#include "pch.h"
#include "D3D11PFX_DistanceBlur.h"
#include "Engine.h"
#include "D3D11GraphicsEngine.h"
#include "D3D11PfxRenderer.h"
#include "RenderToTextureBuffer.h"
#include "D3D11ShaderManager.h"
#include "D3D11VShader.h"
#include "D3D11PShader.h"
#include "D3D11ConstantBuffer.h"
#include "ConstantBufferStructs.h"
#include "GothicAPI.h"

D3D11PFX_DistanceBlur::D3D11PFX_DistanceBlur( D3D11PfxRenderer* rnd ) : D3D11PFX_Effect( rnd ) {}

D3D11PFX_DistanceBlur::~D3D11PFX_DistanceBlur() {}

/** Draws this effect to the given buffer */
XRESULT D3D11PFX_DistanceBlur::Render( ID3D11ShaderResourceView* diffuse ) {
	D3D11GraphicsEngine* engine = reinterpret_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);

	// Save old rendertargets
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
	engine->GetContext()->OMGetRenderTargets( 1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf() );

	engine->GetShaderManager().GetVShader( VShaderID::VS_PFX )->Apply();
	auto ps = engine->GetShaderManager().GetPShader( PShaderID::PS_PFX_DistanceBlur );

	Engine::GAPI->GetRendererState().BlendState.SetDefault();
	Engine::GAPI->GetRendererState().BlendState.SetDirty();

	// Copy scene
    auto tempBuffer = FxRenderer->GetTempBuffer();
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceSRV( diffuse );

	const float clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
	engine->GetContext()->ClearRenderTargetView( tempBuffer->GetRenderTargetView().Get(), clearColor );
    FxRenderer->CopyTextureToRTV( sourceSRV, tempBuffer->GetRenderTargetView(), Engine::GraphicsEngine->GetResolution() );

	engine->GetContext()->OMSetRenderTargets( 1, oldRTV.GetAddressOf(), nullptr );

	// Bind textures
    tempBuffer->BindToPixelShader( engine->GetContext().Get(), 0 );
	engine->GetDepthBuffer()->BindToPixelShader( engine->GetContext().Get(), 1 );

	// Blur/Copy
	// The normalized 1.0 setting represents four times the former 0.5 default.
	const float blurStrength = std::clamp( Engine::GAPI->GetRendererState().RendererSettings.DistanceBlurStrength * 2.0f, 0.0f, 4.0f );
	BlurConstantBuffer bcb = {};
	bcb.B_PixelSize = float2( 1.0f / Engine::GraphicsEngine->GetResolution().x, 1.0f / Engine::GraphicsEngine->GetResolution().y );
	bcb.B_BlurSize = 1.20f + blurStrength * 2.05f;
	bcb.B_Threshold = 6500.0f;
	bcb.B_ColorMod = float4( blurStrength * 0.74f, 26000.0f, 0, 0 );
	XMStoreFloat4x4( &bcb.B_InvProj, XMMatrixInverse( nullptr, XMLoadFloat4x4( &Engine::GAPI->GetProjectionMatrix() ) ) );
	ps->GetBuffer( "B_BlurSettings" ).Update( &bcb ).Bind();
	ps->Apply();

	FxRenderer->DrawFullScreenQuad();

	FxRenderer->UnbindPSResources( 2 );

	// Restore rendertargets
	engine->GetContext()->OMSetRenderTargets( 1, oldRTV.GetAddressOf(), oldDSV.Get() );

	return XR_SUCCESS;
}

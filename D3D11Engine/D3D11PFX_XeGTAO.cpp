#include "pch.h"
#include "D3D11PFX_XeGTAO.h"

#include "D3D11CShader.h"
#include "D3D11ConstantBuffer.h"
#include "D3D11GraphicsEngine.h"
#include "D3D11PShader.h"
#include "D3D11PfxRenderer.h"
#include "D3D11ShaderManager.h"
#include "D3D11VShader.h"
#include "Engine.h"
#include "GothicAPI.h"
#include "RenderToTextureBuffer.h"
#include "Shaders/XeGTAO/XeGTAO.h"
#include <algorithm>

namespace {
    constexpr UINT XeGTAODepthMipCount = XE_GTAO_DEPTH_MIP_LEVELS;

    struct AOCompositeConstantBuffer {
        float Strength;
        float Padding[3];
    };
}

D3D11PFX_XeGTAO::D3D11PFX_XeGTAO( D3D11PfxRenderer* renderer )
    : D3D11PFX_Effect( renderer ) {
}

void D3D11PFX_XeGTAO::ReleaseResources() {
    m_workingDepth.Reset();
    m_workingDepthSRV.Reset();
    for ( auto& uav : m_workingDepthUAVs ) uav.Reset();
    m_aoTermA = {};
    m_aoTermB = {};
    m_edges.Reset();
    m_edgesSRV.Reset();
    m_edgesUAV.Reset();
    m_hilbertLUT.Reset();
    m_hilbertLUTSRV.Reset();
    m_pointClampSampler.Reset();
    m_width = 0;
    m_height = 0;
}

bool D3D11PFX_XeGTAO::CreateAOTermTexture( UINT width, UINT height, AOTermTexture& target ) {
    auto* engine = reinterpret_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);
    auto* device = engine->GetDevice().Get();

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8_TYPELESS;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if ( FAILED( device->CreateTexture2D( &textureDesc, nullptr, target.texture.ReleaseAndGetAddressOf() ) ) ) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Format = DXGI_FORMAT_R8_UINT;
    if ( FAILED( device->CreateShaderResourceView( target.texture.Get(), &srvDesc, target.uintSRV.ReleaseAndGetAddressOf() ) ) ) return false;
    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    if ( FAILED( device->CreateShaderResourceView( target.texture.Get(), &srvDesc, target.unormSRV.ReleaseAndGetAddressOf() ) ) ) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Format = DXGI_FORMAT_R8_UINT;
    if ( FAILED( device->CreateUnorderedAccessView( target.texture.Get(), &uavDesc, target.uintUAV.ReleaseAndGetAddressOf() ) ) ) return false;
    return true;
}

bool D3D11PFX_XeGTAO::EnsureResources( UINT width, UINT height ) {
    if ( m_width == width && m_height == height && m_workingDepth && m_aoTermA.texture && m_aoTermB.texture ) return true;

    ReleaseResources();
    auto* engine = reinterpret_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);
    auto* device = engine->GetDevice().Get();

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = XeGTAODepthMipCount;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R16_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if ( FAILED( device->CreateTexture2D( &depthDesc, nullptr, m_workingDepth.ReleaseAndGetAddressOf() ) ) ) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC depthSRVDesc = {};
    depthSRVDesc.Format = DXGI_FORMAT_R16_FLOAT;
    depthSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    depthSRVDesc.Texture2D.MipLevels = XeGTAODepthMipCount;
    if ( FAILED( device->CreateShaderResourceView( m_workingDepth.Get(), &depthSRVDesc, m_workingDepthSRV.ReleaseAndGetAddressOf() ) ) ) return false;

    for ( UINT mip = 0; mip < XeGTAODepthMipCount; ++mip ) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = mip;
        if ( FAILED( device->CreateUnorderedAccessView( m_workingDepth.Get(), &uavDesc, m_workingDepthUAVs[mip].ReleaseAndGetAddressOf() ) ) ) return false;
    }

    if ( !CreateAOTermTexture( width, height, m_aoTermA ) || !CreateAOTermTexture( width, height, m_aoTermB ) ) return false;

    D3D11_TEXTURE2D_DESC edgeDesc = {};
    edgeDesc.Width = width;
    edgeDesc.Height = height;
    edgeDesc.MipLevels = 1;
    edgeDesc.ArraySize = 1;
    edgeDesc.Format = DXGI_FORMAT_R8_UNORM;
    edgeDesc.SampleDesc.Count = 1;
    edgeDesc.Usage = D3D11_USAGE_DEFAULT;
    edgeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if ( FAILED( device->CreateTexture2D( &edgeDesc, nullptr, m_edges.ReleaseAndGetAddressOf() ) ) ) return false;
    if ( FAILED( device->CreateShaderResourceView( m_edges.Get(), nullptr, m_edgesSRV.ReleaseAndGetAddressOf() ) ) ) return false;
    if ( FAILED( device->CreateUnorderedAccessView( m_edges.Get(), nullptr, m_edgesUAV.ReleaseAndGetAddressOf() ) ) ) return false;

    std::array<uint16_t, 64 * 64> hilbertData = {};
    for ( uint32_t y = 0; y < 64; ++y ) {
        for ( uint32_t x = 0; x < 64; ++x ) hilbertData[y * 64 + x] = static_cast<uint16_t>(XeGTAO::HilbertIndex( x, y ));
    }
    D3D11_TEXTURE2D_DESC lutDesc = {};
    lutDesc.Width = 64;
    lutDesc.Height = 64;
    lutDesc.MipLevels = 1;
    lutDesc.ArraySize = 1;
    lutDesc.Format = DXGI_FORMAT_R16_UINT;
    lutDesc.SampleDesc.Count = 1;
    lutDesc.Usage = D3D11_USAGE_IMMUTABLE;
    lutDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA lutData = {};
    lutData.pSysMem = hilbertData.data();
    lutData.SysMemPitch = 64 * sizeof(uint16_t);
    if ( FAILED( device->CreateTexture2D( &lutDesc, &lutData, m_hilbertLUT.ReleaseAndGetAddressOf() ) ) ) return false;
    if ( FAILED( device->CreateShaderResourceView( m_hilbertLUT.Get(), nullptr, m_hilbertLUTSRV.ReleaseAndGetAddressOf() ) ) ) return false;

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if ( FAILED( device->CreateSamplerState( &samplerDesc, m_pointClampSampler.ReleaseAndGetAddressOf() ) ) ) return false;

    m_width = width;
    m_height = height;
    return true;
}

XRESULT D3D11PFX_XeGTAO::Render( ID3D11ShaderResourceView* depthSRV,
                                  ID3D11ShaderResourceView* normalsSRV,
                                  ID3D11RenderTargetView* outputRTV ) {
    if ( !depthSRV || !normalsSRV || !outputRTV ) return XR_FAILED;

    auto* engine = reinterpret_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);
    auto& context = engine->GetContext();
    const INT2 resolution = engine->GetResolution();
    if ( resolution.x <= 0 || resolution.y <= 0 || !EnsureResources( resolution.x, resolution.y ) ) return XR_FAILED;

    auto& rendererSettings = Engine::GAPI->GetRendererState().RendererSettings;
    auto& settings = rendererSettings.XegtaoSettings;
    auto projection = Engine::GAPI->GetProjectionMatrix();

    XeGTAO::GTAOSettings gtaoSettings;
    gtaoSettings.QualityLevel = std::clamp( settings.QualityLevel, 0, 3 );
    gtaoSettings.DenoisePasses = std::clamp( settings.DenoisePasses, 1, 3 );
    gtaoSettings.Radius = std::max( 1.0f, settings.Radius );

    const CShaderID qualityShaders[] = {
        CShaderID::CS_PFX_XeGTAO_Low,
        CShaderID::CS_PFX_XeGTAO_Medium,
        CShaderID::CS_PFX_XeGTAO_High,
        CShaderID::CS_PFX_XeGTAO_Ultra
    };
    auto prefilter = engine->GetShaderManager().GetCShader( CShaderID::CS_PFX_XeGTAO_Prefilter );
    auto mainPass = engine->GetShaderManager().GetCShader( qualityShaders[gtaoSettings.QualityLevel] );
    auto denoisePass = engine->GetShaderManager().GetCShader( CShaderID::CS_PFX_XeGTAO_Denoise );
    auto denoiseLastPass = engine->GetShaderManager().GetCShader( CShaderID::CS_PFX_XeGTAO_DenoiseLast );
    auto fullscreenVS = engine->GetShaderManager().GetVShader( VShaderID::VS_PFX );
    auto composite = engine->GetShaderManager().GetPShader( PShaderID::PS_PFX_AOComposite );
    if ( !prefilter || !mainPass || !denoisePass || !denoiseLastPass || !fullscreenVS || !composite ) return XR_FAILED;

    XeGTAO::GTAOConstants constants = {};
    XeGTAO::GTAOUpdateConstants( constants, resolution.x, resolution.y, gtaoSettings,
        reinterpret_cast<const float*>(&projection), true, m_frameIndex );
    constants.NoiseIndex = rendererSettings.GetIsTAAEnabled() ? static_cast<int>(m_frameIndex % 64) : 0;
    ++m_frameIndex;

    ID3D11RenderTargetView* nullRTVs[8] = {};
    ID3D11ShaderResourceView* nullSRVs[8] = {};
    ID3D11UnorderedAccessView* nullUAVs[5] = {};
    context->OMSetRenderTargets( 8, nullRTVs, nullptr );
    context->CSSetShaderResources( 0, 8, nullSRVs );
    context->CSSetSamplers( 0, 1, m_pointClampSampler.GetAddressOf() );

    prefilter->Apply();
    prefilter->GetBuffer( "GTAOConstantBuffer" ).Update( &constants ).Bind();
    context->CSSetShaderResources( 0, 1, &depthSRV );
    ID3D11UnorderedAccessView* depthUAVs[XeGTAODepthMipCount] = {};
    for ( UINT i = 0; i < XeGTAODepthMipCount; ++i ) depthUAVs[i] = m_workingDepthUAVs[i].Get();
    context->CSSetUnorderedAccessViews( 0, XeGTAODepthMipCount, depthUAVs, nullptr );
    context->Dispatch( (resolution.x + 15) / 16, (resolution.y + 15) / 16, 1 );
    context->CSSetUnorderedAccessViews( 0, XeGTAODepthMipCount, nullUAVs, nullptr );
    context->CSSetShaderResources( 0, 8, nullSRVs );

    mainPass->Apply();
    mainPass->GetBuffer( "GTAOConstantBuffer" ).Update( &constants ).Bind();
    ID3D11ShaderResourceView* mainSRVs[6] = { m_workingDepthSRV.Get(), normalsSRV, nullptr, nullptr, nullptr, m_hilbertLUTSRV.Get() };
    context->CSSetShaderResources( 0, 6, mainSRVs );
    ID3D11UnorderedAccessView* mainUAVs[2] = { m_aoTermA.uintUAV.Get(), m_edgesUAV.Get() };
    context->CSSetUnorderedAccessViews( 0, 2, mainUAVs, nullptr );
    context->Dispatch( (resolution.x + 7) / 8, (resolution.y + 7) / 8, 1 );
    context->CSSetUnorderedAccessViews( 0, 2, nullUAVs, nullptr );
    context->CSSetShaderResources( 0, 8, nullSRVs );

    AOTermTexture* source = &m_aoTermA;
    AOTermTexture* destination = &m_aoTermB;
    for ( int pass = 0; pass < gtaoSettings.DenoisePasses; ++pass ) {
        const bool lastPass = pass == gtaoSettings.DenoisePasses - 1;
        auto denoise = lastPass ? denoiseLastPass : denoisePass;
        denoise->Apply();
        denoise->GetBuffer( "GTAOConstantBuffer" ).Update( &constants ).Bind();
        ID3D11ShaderResourceView* denoiseSRVs[2] = { source->uintSRV.Get(), m_edgesSRV.Get() };
        context->CSSetShaderResources( 0, 2, denoiseSRVs );
        ID3D11UnorderedAccessView* outputUAV = destination->uintUAV.Get();
        context->CSSetUnorderedAccessViews( 0, 1, &outputUAV, nullptr );
        context->Dispatch( (resolution.x + 15) / 16, (resolution.y + 7) / 8, 1 );
        context->CSSetUnorderedAccessViews( 0, 1, nullUAVs, nullptr );
        context->CSSetShaderResources( 0, 8, nullSRVs );
        std::swap( source, destination );
    }
    context->CSSetShader( nullptr, nullptr, 0 );

    Engine::GAPI->GetRendererState().BlendState.SetModulateBlending();
    Engine::GAPI->GetRendererState().BlendState.SetDirty();
    Engine::GAPI->GetRendererState().DepthState.DepthBufferCompareFunc = GothicDepthBufferStateInfo::CF_COMPARISON_ALWAYS;
    Engine::GAPI->GetRendererState().DepthState.DepthWriteEnabled = false;
    Engine::GAPI->GetRendererState().DepthState.SetDirty();

    fullscreenVS->Apply();
    composite->Apply();
    // UI-normalized XeGTAO strength: 1.0 maps to the selected 60% composite strength.
    constexpr float XeGTAONormalizedStrength = 0.6f;
    AOCompositeConstantBuffer compositeConstants = {
        rendererSettings.AOStrength * XeGTAONormalizedStrength, {}
    };
    composite->GetBuffer( "AOCompositeConstantBuffer" ).Update( &compositeConstants ).Bind();

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(resolution.x);
    viewport.Height = static_cast<float>(resolution.y);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports( 1, &viewport );
    context->OMSetRenderTargets( 1, &outputRTV, nullptr );
    ID3D11ShaderResourceView* finalAO = source->unormSRV.Get();
    context->PSSetShaderResources( 0, 1, &finalAO );
    context->PSSetSamplers( 0, 1, m_pointClampSampler.GetAddressOf() );
    FxRenderer->DrawFullScreenQuad();
    context->PSSetShaderResources( 0, 1, nullSRVs );

    Engine::GAPI->GetRendererState().BlendState.SetDefault();
    Engine::GAPI->GetRendererState().BlendState.SetDirty();
    Engine::GAPI->GetRendererState().DepthState.DepthBufferCompareFunc = GothicDepthBufferStateInfo::DEFAULT_DEPTH_COMP_STATE;
    Engine::GAPI->GetRendererState().DepthState.DepthWriteEnabled = true;
    Engine::GAPI->GetRendererState().DepthState.SetDirty();
    return XR_SUCCESS;
}

#include "pch.h"
#include "D3D11DeferredRenderer.h"
#include "D3D11GraphicsEngine.h"
#include "D3D11ShaderManager.h"
#include "D3D11ShadowMap.h"
#include "D3D11PShader.h"
#include "RenderGraph.h"
#include "RenderToTextureBuffer.h"
#include "zCTexture.h"
#include "zCMaterial.h"
#include "Engine.h"
#include "GothicAPI.h"
#include "GothicGraphicsState.h"
#include "GSky.h"

static ID3D11ShaderResourceView* s_nullSRVs[16] = { nullptr };

void D3D11DeferredRenderer::AddGeometryPasses( RenderGraph& graph,
    D3D11GraphicsEngine& engine,
    RGResourceHandle colorResource,
    RGResourceHandle velocityBufferHandle,
    RGResourceHandle backBufferHandle,
    RGResourceHandle& outNormalsResource,
    RGResourceHandle& outSpecularResource,
    RGResourceHandle& outReactiveMaskResource,
    RGResourceHandle& outTransparencyAndCompositionMaskResource ) {

    RGResourceHandle normalsResource;
    RGResourceHandle specularResource;
    RGResourceHandle reactiveMaskResource;
    RGResourceHandle transparencyAndCompositionMaskResource;

    graph.AddPass( RG_PASS_NAME("G-Buffer Pass"), [&, colorResource, velocityBufferHandle, backBufferHandle]( RGBuilder& builder, RenderPass& pass ) {
        auto size = engine.GetResolution();
        normalsResource = builder.CreateTexture( { static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), DXGI_FORMAT_R16G16_FLOAT, L"GBufferNormals" } );
        specularResource = builder.CreateTexture( { static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), DXGI_FORMAT_R16G16_FLOAT, L"GBufferSpecular" } );
        reactiveMaskResource = builder.CreateTexture( { static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), DXGI_FORMAT_R8_UNORM, L"ReactiveMask" } );
        transparencyAndCompositionMaskResource = builder.CreateTexture( { static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), DXGI_FORMAT_R8_UNORM, L"TransparencyAndCompositionMask" } );
        builder.Write( colorResource );
        builder.Write( normalsResource );
        builder.Write( specularResource );
        builder.Write( velocityBufferHandle );
        builder.Write( reactiveMaskResource );
        builder.Write( transparencyAndCompositionMaskResource );
        builder.Write( backBufferHandle );

        pass.m_executeCallback = [&engine, colorResource, normalsResource, specularResource, reactiveMaskResource, transparencyAndCompositionMaskResource, velocityBufferHandle]( const RenderGraph& graph ) -> void {
            TracyD3D11ZoneCGX( "D3D11DeferredRenderer::G-Buffer Pass" );
            const auto& context = engine.GetContext();
            context->VSSetShaderResources( 0, 8, s_nullSRVs );
            context->PSSetShaderResources( 0, 8, s_nullSRVs );
            context->DSSetShaderResources( 0, 8, s_nullSRVs );
            context->HSSetShaderResources( 0, 8, s_nullSRVs );
            context->CSSetShaderResources( 0, 8, s_nullSRVs );

            auto normals = graph.GetPhysicalTexture( normalsResource );
            auto specular = graph.GetPhysicalTexture( specularResource );
            auto reactiveMask = graph.GetPhysicalTexture( reactiveMaskResource );
            auto transparencyAndCompositionMask = graph.GetPhysicalTexture( transparencyAndCompositionMaskResource );
            auto velocityBuffer = graph.GetPhysicalTexture( velocityBufferHandle );

            const auto aaMode = Engine::GAPI->GetRendererState().RendererSettings.AntiAliasingMode;
            if ( aaMode != GothicRendererSettings::AA_TAA
                && aaMode != GothicRendererSettings::AA_FSR
                && aaMode != GothicRendererSettings::AA_FSR3 ) {
                velocityBuffer = nullptr; // don't write velocity if not needed.
                // NOTE: we should automate this, by putting the velocity 
                // buffer creation INTO the rendergraph instead of passing it in via external handle
            }
            ID3D11RenderTargetView* rtvs[] = {
                graph.GetPhysicalTexture( colorResource )->GetRenderTargetView().Get(),
                normals ? normals->GetRenderTargetView().Get() : nullptr,
                specular ? specular->GetRenderTargetView().Get() : nullptr,
                velocityBuffer ? velocityBuffer->GetRenderTargetView().Get() : nullptr,
                transparencyAndCompositionMask ? transparencyAndCompositionMask->GetRenderTargetView().Get() : nullptr,
                reactiveMask ? reactiveMask->GetRenderTargetView().Get() : nullptr,
            };

            constexpr float black[] { 0.f, 0.f, 0.f, 0.f };
            // Skip color and the T&C mask; clear normals, specular and velocity.
            for ( size_t i = 1; i + 1 < std::size( rtvs ); i++ ) {
                if ( rtvs[i] )
                    context->ClearRenderTargetView( rtvs[i], black );
            }

            // Sky is rendered later without depth or MRT writes. With FSR3 active, opaque
            // geometry overwrites this value with 0 and uncovered sky remains fully marked.
            const auto& rendererSettings = Engine::GAPI->GetRendererState().RendererSettings;
            const bool fsr3Active = rendererSettings.AntiAliasingMode == GothicRendererSettings::AA_FSR3
                || (rendererSettings.AntiAliasingMode == GothicRendererSettings::AA_FSR
                    && rendererSettings.Upscaler == GothicRendererSettings::UPSCALER_FSR_3);
            const float skyTncValue = fsr3Active ? 1.f : 0.f;
            const float skyTransparencyAndComposition[] { skyTncValue, skyTncValue, skyTncValue, skyTncValue };
            if ( reactiveMask )
                context->ClearRenderTargetView( reactiveMask->GetRenderTargetView().Get(), black );
            if ( rtvs[4] )
                context->ClearRenderTargetView( rtvs[4], skyTransparencyAndComposition );
            context->OMSetRenderTargets( 6, rtvs, engine.GetDepthBuffer()->GetDepthStencilView().Get() );

            Engine::GAPI->DrawWorldMeshNaive();

            engine.SetViewport( ViewportInfo( 0, 0, engine.GetResolution() ) );

            engine.StoreVobPreviousTransforms();
        };
    } );

    outNormalsResource = normalsResource;
    outSpecularResource = specularResource;
    outReactiveMaskResource = reactiveMaskResource;
    outTransparencyAndCompositionMaskResource = transparencyAndCompositionMaskResource;
}

void D3D11DeferredRenderer::AddLightingPasses( RenderGraph& graph,
    D3D11GraphicsEngine& engine,
    RGResourceHandle colorResource,
    RGResourceHandle normalsResource,
    RGResourceHandle specularResource,
    RGResourceHandle reactiveMaskResource,
    RGResourceHandle backBufferHandle,
    std::vector<VobLightInfo*>& frameLights ) {

    graph.AddPass( RG_PASS_NAME("Draw Lighting"), [&, colorResource, normalsResource, specularResource, reactiveMaskResource, backBufferHandle]( RGBuilder& builder, RenderPass& pass ) {
        builder.Read( colorResource );
        builder.Read( normalsResource );
        builder.Read( specularResource );
        builder.Read( reactiveMaskResource );
        builder.Write( backBufferHandle );

        pass.m_executeCallback = [&engine, &frameLights, colorResource, normalsResource, specularResource, reactiveMaskResource]( const RenderGraph& graph ) -> void {
            TracyD3D11ZoneCGX( "D3D11DeferredRenderer::Draw Lighting" );
            auto colorTexture = graph.GetPhysicalTexture( colorResource );
            auto normalsTexture = graph.GetPhysicalTexture( normalsResource );
            auto specularTexture = graph.GetPhysicalTexture( specularResource );
            auto reactiveMaskTexture = graph.GetPhysicalTexture( reactiveMaskResource );

            engine.CopyDepthStencil(); // always needed due to depth testing!

            engine.GetShadowMaps()->DrawLighting( frameLights,
                *colorTexture,
                *normalsTexture,
                *specularTexture,
                *reactiveMaskTexture,
                *engine.GetDepthBufferCopy() );

            if ( !Engine::GAPI->GetRendererState().RendererSettings.FixViewFrustum ) {
                frameLights.clear();
            }
        };
    } );
}

bool D3D11DeferredRenderer::BindShaderForTexture( D3D11ShaderManager& shaderManager,
    std::shared_ptr<D3D11PShader>& activePS,
    zCTexture* texture,
    bool forceAlphaTest,
    int zMatAlphaFunc,
    MaterialInfo::EMaterialType materialType,
    PShaderID resolvedDiffuseNormalmapped,
    PShaderID resolvedDiffuseNormalmappedFxMap,
    PShaderID resolvedDiffuseNormalmappedAlphatest,
    PShaderID resolvedDiffuseNormalmappedAlphatestFxMap,
    bool allowWetNormalFallback ) {

    auto active = activePS;
    auto newShader = activePS;
    bool bindParticleAtmosphere = false;

    bool blendAdd = zMatAlphaFunc == zMAT_ALPHA_FUNC_ADD;
    bool blendBlend = zMatAlphaFunc == zMAT_ALPHA_FUNC_BLEND;
    bool linZ = (Engine::GAPI->GetRendererState().GraphicsState.FF_GSwitches & GSWITCH_LINEAR_DEPTH) != 0;
    const bool normalmapsEnabled = Engine::GAPI->GetRendererState().RendererSettings.AllowNormalmaps;
    const bool hasNormalmap = normalmapsEnabled && texture->GetSurface()->GetNormalmap() != nullptr;
    const bool useWetNormalFallback = allowWetNormalFallback && !hasNormalmap && Engine::GAPI->GetSceneWetness() > 1e-6f;
    const bool useNormalmapShader = hasNormalmap || useWetNormalFallback;
    const bool hasFxMap = hasNormalmap && texture->GetSurface()->GetFxMap();

    if ( materialType == MaterialInfo::MT_Portal ) {
        newShader = shaderManager.GetPShader( PShaderID::PS_PortalDiffuse );
    } else if ( materialType == MaterialInfo::MT_WaterfallFoam ) {
        newShader = shaderManager.GetPShader( PShaderID::PS_WaterfallFoam );
    } else if ( linZ ) {
        newShader = shaderManager.GetPShader( PShaderID::PS_LinDepth );
    } else if ( blendAdd || blendBlend ) {
        newShader = shaderManager.GetPShader( PShaderID::PS_ParticleSimple_FF );
        bindParticleAtmosphere = true;
    } else if ( texture->HasAlphaChannel() || forceAlphaTest ) {
        if ( hasFxMap ) {
            newShader = shaderManager.GetPShader( resolvedDiffuseNormalmappedAlphatestFxMap );
        } else if ( useNormalmapShader ) {
            newShader = shaderManager.GetPShader( useWetNormalFallback
                ? PShaderID::PS_DiffuseNormalmappedAlphaTest
                : resolvedDiffuseNormalmappedAlphatest );
        } else {
            newShader = shaderManager.GetPShader( PShaderID::PS_DiffuseAlphaTest );
        }
    } else {
        if ( hasFxMap ) {
            newShader = shaderManager.GetPShader( resolvedDiffuseNormalmappedFxMap );
        } else if ( useNormalmapShader ) {
            newShader = shaderManager.GetPShader( useWetNormalFallback
                ? PShaderID::PS_DiffuseNormalmapped
                : resolvedDiffuseNormalmapped );
        } else {
            newShader = shaderManager.GetPShader( PShaderID::PS_Diffuse );
        }
    }

    bool changed = active != newShader;
    if ( changed ) {
        activePS = newShader;
        activePS->Apply();
    }
    if ( materialType == MaterialInfo::MT_WaterfallFoam || bindParticleAtmosphere ) {
        if ( GSky* sky = Engine::GAPI->GetSky() ) {
            activePS->GetBuffer( "Atmosphere" ).Update( &sky->GetAtmosphereCB() ).Bind();
        }
    }
    return changed;
}

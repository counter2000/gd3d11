#include "D3D11Upscaling.h"
#include "Engine.h"
#include "GothicAPI.h"
#include "D3D11GraphicsEngine.h"
#include "D3D11PfxRenderer.h"
#include "D3D11PFX_FSR3.h"
#include "D3D11PFX_TAA.h"
#include "oCGame.h"

namespace {

    void AddFSR3Pass( RenderGraph& graph,
        D3D11GraphicsEngine& engine,
        ID3D11RenderTargetView* outputRTV,
        RGResourceHandle backBufferHandle,
        ID3D11ShaderResourceView* depth,
        RGResourceHandle velocityBufferHandle,
        RGResourceHandle reactiveMaskResource,
        RGResourceHandle transparencyAndCompositionMaskResource )
    {
        graph.AddPass( RG_PASS_NAME("FSR 3"), [&]( RGBuilder& builder, RenderPass& pass ) {
            builder.Read( velocityBufferHandle );
            builder.Read( reactiveMaskResource );
            builder.Read( transparencyAndCompositionMaskResource );
            builder.Read( backBufferHandle );

            builder.Write( backBufferHandle );

            pass.m_executeCallback = [&engine, backBufferHandle, outputRTV, velocityBufferHandle, reactiveMaskResource, transparencyAndCompositionMaskResource, depth]( const RenderGraph& graph ) {
                auto& settings = Engine::GAPI->GetRendererState().RendererSettings;

                auto backbufferTex = graph.GetPhysicalTexture( backBufferHandle );

                auto sharpenFactor = settings.SharpenFactor;

                auto velocityBufferTex = graph.GetPhysicalTexture( velocityBufferHandle );
                auto reactiveMask = graph.GetPhysicalTexture( reactiveMaskResource );
                auto transparencyAndCompositionMask = graph.GetPhysicalTexture( transparencyAndCompositionMaskResource );

                auto* pfxRenderer = engine.GetPfxRenderer();
                auto* fsr3 = pfxRenderer ? pfxRenderer->GetFSR3() : nullptr;
                auto* taa = pfxRenderer ? pfxRenderer->GetTAAEffect() : nullptr;
                if ( !fsr3 || !taa || !backbufferTex || !velocityBufferTex || !depth || !outputRTV ) {
                    LogError() << "FSR3: Upscaling pass skipped because required resources are missing.";
                    return;
                }

                auto jitter = taa->GetJitterOffsetUnscaled();
                const auto inputSize = engine.GetResolution();

                float fovHorizontal, fovVertical;
                auto* game = oCGame::GetGame();
                auto cam = game ? ((zCCamera*)game->_zCSession_camera) : nullptr;
                if ( !cam ) {
                    LogError() << "FSR3: Upscaling pass skipped because Gothic camera is missing.";
                    return;
                }
                cam->GetFOV( fovHorizontal, fovVertical );

                // With ENABLE_DEPTH_INVERTED | ENABLE_DEPTH_INFINITE flags,
                // FSR expects inverted metrics: cameraNear=FLT_MAX (infinity),
                // cameraFar=actual near plane (since depth is inverted: 1=near, 0=far)
                float nearZ = FLT_MAX;  // Infinity in view space
                float farZ = cam ? cam->GetNearPlane() : 0.01f;
                farZ = std::max( farZ, 0.075f );  // FSR3 validation requires cameraFar >= 0.075f

                ID3D11SamplerState* linearSampler = engine.GetLinearSamplerState();
                engine.GetContext()->CSSetSamplers( 0, 1, &linearSampler );
                engine.GetContext()->CSSetSamplers( 1, 1, &linearSampler );

                ID3D11Buffer* nullCBs[5]{};
                engine.GetContext()->CSSetConstantBuffers( 0, std::size( nullCBs ), nullCBs );

                fsr3->Apply(
                    backbufferTex->GetShaderResView().Get(),
                    depth,
                    velocityBufferTex->GetShaderResView().Get(),
                    reactiveMask ? reactiveMask->GetShaderResView().Get() : nullptr,
                    transparencyAndCompositionMask ? transparencyAndCompositionMask->GetShaderResView().Get() : nullptr,
                    outputRTV,
                    inputSize,
                    engine.GetBackbufferResolution(),
                    Engine::GAPI->GetDeltaTime() * 1000.f,
                    jitter,
                    float2( static_cast<float>(inputSize.x), static_cast<float>(inputSize.y) ),
                    false,
                    fovVertical,
                    nearZ,
                    farZ,
                    sharpenFactor >= 0.001f,
                    sharpenFactor /* FSR3 has 0..1 (sharp)*/ );
                };
        } );
    }
}

void D3D11Upscaling::UpdateUpscaling( D3D11GraphicsEngine& engine )
{
    auto& settings = Engine::GAPI->GetRendererState().RendererSettings;
    if ( engine.GetDevice()->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ) {
        if ( settings.AntiAliasingMode == GothicRendererSettings::AA_FSR
            && settings.Upscaler == GothicRendererSettings::UPSCALER_FSR_3 ) {
            settings.AntiAliasingMode = GothicRendererSettings::AA_SMAA;
            settings.Upscaler = GothicRendererSettings::UPSCALER_DEFAULT;
            settings.ResolutionScalePercent = 100;
            settings.SharpenFactor = 0.2f;
            settings.EnableFrameGeneration = false;
        }
        return;
    }

    const bool needsJitteredProj = settings.AntiAliasingMode == GothicRendererSettings::AA_FSR
        || settings.AntiAliasingMode == GothicRendererSettings::AA_TAA
        || settings.AntiAliasingMode == GothicRendererSettings::AA_FSR3;

    if ( needsJitteredProj ) {
        engine.SetFrameNeedsJitter();
    }
}

bool D3D11Upscaling::AddUpscalingPass( RenderGraph& graph,
    D3D11GraphicsEngine& engine, 
    ID3D11RenderTargetView* outputRTV,
    RGResourceHandle color, 
    ID3D11ShaderResourceView* depth,
    RGResourceHandle motionVectors,
    RGResourceHandle reactiveMask,
    RGResourceHandle transparencyAndCompositionMask )
{

    auto& settings = Engine::GAPI->GetRendererState().RendererSettings;


    if ( engine.GetDevice()->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ) {
        return false;
    }

    auto* pfxRenderer = engine.GetPfxRenderer();
    if ( settings.Upscaler == GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3
            && (settings.ResolutionScalePercent <= 100)
            && settings.AntiAliasingMode == GothicRendererSettings::AA_FSR
            && pfxRenderer
            && pfxRenderer->GetFSR3()
            && pfxRenderer->GetTAAEffect() ) {

        AddFSR3Pass( graph, engine, outputRTV, color, depth, motionVectors, reactiveMask,
            transparencyAndCompositionMask );
        return true;
    }
    return false;
}

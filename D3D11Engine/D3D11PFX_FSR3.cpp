#include "pch.h"

#include "D3D11PFX_FSR3.h"

#include "D3D11PfxRenderer.h"
#include "D3D11GraphicsEngine.h"
#include "Engine.h"
#include "GothicAPI.h"
#include "ImGuiShim.h"
#include "RenderToTextureBuffer.h"
#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <algorithm>
#include <cstdlib>

#pragma comment(lib, "ffx_backend_dx11_x86.lib")
#pragma comment(lib, "ffx_fsr3upscaler_x86.lib")

namespace {
    enum BackendIndex {
        BACKEND_SHARED_RESOURCES = 0,
        BACKEND_UPSCALING = 1,
        BACKEND_FRAME_INTERPOLATION = 2,
        BACKEND_COUNT = 3,
    };

    FfxResource WrapResource(
        ID3D11Resource* resource,
        const wchar_t* name,
        FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ )
    {
        if ( !resource ) {
            return {};
        }
        FfxResource ffxResource = {};
        ffxResource.resource = resource;
        ffxResource.description = GetFfxResourceDescriptionDX11( resource );
        ffxResource.state = state;
        if ( name ) {
            wcscpy_s( ffxResource.name, name );
        }
        return ffxResource;
    }

    ID3D11Resource* GetResourceFromView( ID3D11View* view ) {
        if ( !view ) {
            return nullptr;
        }
        ID3D11Resource* resource = nullptr;
        view->GetResource( &resource );
        if ( resource ) {
            resource->Release();
        }
        return resource;
    }

    FfxResource WrapView(
        ID3D11View* view,
        const wchar_t* name,
        FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ )
    {
        return WrapResource( GetResourceFromView( view ), name, state );
    }

    void UnbindComputeResources( ID3D11DeviceContext* context ) {
        ID3D11ShaderResourceView* nullSrvs[16] = {};
        ID3D11UnorderedAccessView* nullUavs[8] = {};
        UINT initialCounts[8] = {};
        context->CSSetShaderResources( 0, static_cast<UINT>(std::size( nullSrvs )), nullSrvs );
        context->CSSetUnorderedAccessViews( 0, static_cast<UINT>(std::size( nullUavs )), nullUavs, initialCounts );
        context->CSSetShader( nullptr, nullptr, 0 );
    }

    void UnbindFrameGenerationInputs( ID3D11DeviceContext* context ) {
        ID3D11RenderTargetView* nullRtvs[8] = {};
        ID3D11ShaderResourceView* nullSrvs[16] = {};
        context->OMSetRenderTargets( static_cast<UINT>(std::size( nullRtvs )), nullRtvs, nullptr );
        context->VSSetShaderResources( 0, static_cast<UINT>(std::size( nullSrvs )), nullSrvs );
        context->PSSetShaderResources( 0, static_cast<UINT>(std::size( nullSrvs )), nullSrvs );
        UnbindComputeResources( context );
    }

    void FfxLog( FfxMsgType type, const wchar_t* message ) {
        LogError() << "FidelityFX FSR3 (" << type << "): " << message;
    }

    // The third-party DX11 backend throws across the DLL boundary when a D3D11
    // resource call fails. Keep that backend exception from escaping into Gothic;
    // normal FidelityFX error handling can then disable only the failed request.
    FfxErrorCode GuardedFsr3ContextCreate(
        FfxFsr3Context* context,
        FfxFsr3ContextDescription* description )
    {
        __try {
            return ffxFsr3ContextCreate( context, description );
        }
        __except ( EXCEPTION_EXECUTE_HANDLER ) {
            return FFX_ERROR_BACKEND_API_ERROR;
        }
    }
}

D3D11PFX_FSR3::D3D11PFX_FSR3( D3D11PfxRenderer* renderer )
    : Renderer( renderer )
    , Context( nullptr )
    , MaxInputSize( 0, 0 )
    , MaxOutputSize( 0, 0 )
    , Initialized( false )
    , ContextFrameGenerationEnabled( false )
    , FrameGenerationPrepared( false )
    , HudlessCaptured( false )
    , FrameGenerationConfigured( false )
    , ForceFrameGenerationReset( true )
    , PreparedFrameCount( 0 )
    , FrameId( 0 )
    , PreparedFrameId( 0 )
    , DiagnosticsWindowStartMs( 0 )
    , DiagnosticsRenderedFrames( 0 )
    , DiagnosticsPreparedFrames( 0 )
    , DiagnosticsGeneratedFrames( 0 )
    , DiagnosticsPresentedFrames( 0 )
{
    std::fill( std::begin( Backends ), std::end( Backends ), FfxInterface{} );
    std::fill( std::begin( ScratchMemory ), std::end( ScratchMemory ), nullptr );
}

D3D11PFX_FSR3::~D3D11PFX_FSR3() {
    Destroy();
}

bool D3D11PFX_FSR3::Init(
    const INT2& maxInputSize,
    const INT2& maxOutputSize,
    bool enableFrameGeneration )
{
    if ( Initialized
        && MaxInputSize == maxInputSize
        && MaxOutputSize == maxOutputSize
        && ContextFrameGenerationEnabled == enableFrameGeneration ) {
        return true;
    }

    Destroy();

    auto* engine = static_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);
    ID3D11Device* device = engine->GetDevice().Get();
    if ( !device || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ) {
        return false;
    }

    MaxInputSize = maxInputSize;
    MaxOutputSize = maxOutputSize;
    ContextFrameGenerationEnabled = enableFrameGeneration;

    const int effectCounts[BACKEND_COUNT] = {
        FFX_FSR3_CONTEXT_COUNT,
        FFX_FSR3UPSCALER_CONTEXT_COUNT,
        FFX_OPTICALFLOW_CONTEXT_COUNT + FFX_FRAMEINTERPOLATION_CONTEXT_COUNT
    };
    for ( int i = 0; i < BACKEND_COUNT; ++i ) {
        const size_t scratchSize = ffxGetScratchMemorySizeDX11( effectCounts[i] );
        ScratchMemory[i] = calloc( 1, scratchSize );
        if ( !ScratchMemory[i] ) {
            LogError() << "FSR3: Failed to allocate backend scratch memory.";
            Destroy();
            return false;
        }

        const FfxErrorCode interfaceResult = ffxGetInterfaceDX11(
            &Backends[i],
            device,
            ScratchMemory[i],
            scratchSize,
            effectCounts[i] );
        if ( interfaceResult != FFX_OK ) {
            LogError() << "FSR3: Failed to create DX11 backend interface " << i << ".";
            Destroy();
            return false;
        }
    }

    FfxFsr3ContextDescription desc = {};
    desc.flags = FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE
        | FFX_FSR3_ENABLE_AUTO_EXPOSURE
        | FFX_FSR3_ENABLE_DEPTH_INVERTED
        | FFX_FSR3_ENABLE_DEPTH_INFINITE
        | FFX_FSR3_ENABLE_DYNAMIC_RESOLUTION;
    if ( !enableFrameGeneration ) {
        desc.flags |= FFX_FSR3_ENABLE_UPSCALING_ONLY;
    }
#ifdef DEBUG_D3D11
    desc.flags |= FFX_FSR3_ENABLE_DEBUG_CHECKING;
    desc.fpMessage = &FfxLog;
#endif

    desc.maxRenderSize = {
        static_cast<uint32_t>(maxInputSize.x),
        static_cast<uint32_t>(maxInputSize.y)
    };
    desc.maxUpscaleSize = {
        static_cast<uint32_t>(maxOutputSize.x),
        static_cast<uint32_t>(maxOutputSize.y)
    };
    desc.displaySize = desc.maxUpscaleSize;
    desc.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
    desc.backendInterfaceSharedResources = Backends[BACKEND_SHARED_RESOURCES];
    desc.backendInterfaceUpscaling = Backends[BACKEND_UPSCALING];
    desc.backendInterfaceFrameInterpolation = Backends[BACKEND_FRAME_INTERPOLATION];

    Context = new FfxFsr3Context{};
    const FfxErrorCode createResult = GuardedFsr3ContextCreate( Context, &desc );
    if ( createResult != FFX_OK ) {
        LogError() << "FSR3: Failed to create combined upscaling/frame-generation context (" << createResult << ").";
        SAFE_DELETE( Context );
        Destroy();
        return false;
    }

    if ( enableFrameGeneration ) {
        const uint32_t bindFlags = D3D11_BIND_RENDER_TARGET
            | D3D11_BIND_SHADER_RESOURCE
            | D3D11_BIND_UNORDERED_ACCESS;

        HudlessColor = std::make_unique<RenderToTextureBuffer>(
            device, maxOutputSize.x, maxOutputSize.y, DXGI_FORMAT_R8G8B8A8_UNORM,
            nullptr, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, 1, 1,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE );
        PresentColor = std::make_unique<RenderToTextureBuffer>(
            device, maxOutputSize.x, maxOutputSize.y, DXGI_FORMAT_R8G8B8A8_UNORM,
            nullptr, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, 1, 1,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE );
        InterpolatedOutput = std::make_unique<RenderToTextureBuffer>(
            device, maxOutputSize.x, maxOutputSize.y, DXGI_FORMAT_R8G8B8A8_UNORM,
            nullptr, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, 1, 1, bindFlags );

        if ( !HudlessColor->GetShaderResView()
            || !PresentColor->GetShaderResView()
            || !InterpolatedOutput->GetShaderResView()
            || !InterpolatedOutput->GetUnorderedAccessView() ) {
            LogError() << "FSR3: Failed to create frame-generation color resources.";
            Destroy();
            return false;
        }

        SetDebugName( HudlessColor->GetTexture().Get(), "FSR3 Frame Generation HUD-less Color" );
        SetDebugName( PresentColor->GetTexture().Get(), "FSR3 Frame Generation Present Color" );
        SetDebugName( InterpolatedOutput->GetTexture().Get(), "FSR3 Frame Generation Output" );
    }

    Initialized = true;
    ResetFrameGenerationHistory();
    return true;
}

void D3D11PFX_FSR3::Destroy() {
    if ( Context ) {
        try {
            ffxFsr3ContextDestroy( Context );
        } catch ( ... ) {
            LogError() << "FSR3: DX11 backend exception while destroying the context.";
        }
        SAFE_DELETE( Context );
    }

    HudlessColor.reset();
    PresentColor.reset();
    InterpolatedOutput.reset();

    for ( int i = 0; i < BACKEND_COUNT; ++i ) {
        if ( ScratchMemory[i] ) {
            free( ScratchMemory[i] );
            ScratchMemory[i] = nullptr;
        }
        Backends[i] = {};
    }

    Initialized = false;
    ContextFrameGenerationEnabled = false;
    FrameGenerationPrepared = false;
    HudlessCaptured = false;
    FrameGenerationConfigured = false;
    ForceFrameGenerationReset = true;
    PreparedFrameCount = 0;
    FrameId = 0;
    PreparedFrameId = 0;
}

XRESULT D3D11PFX_FSR3::Apply(
    ID3D11ShaderResourceView* color,
    ID3D11ShaderResourceView* depth,
    ID3D11ShaderResourceView* motionVectors,
    ID3D11ShaderResourceView* reactiveMask,
    ID3D11ShaderResourceView* transparencyAndCompositionMask,
    ID3D11RenderTargetView* output,
    const INT2& inputSize,
    const INT2& outputSize,
    float deltaTimeMs,
    const float2& jitterOffset,
    const float2& motionVectorScale,
    bool resetAccumulation,
    float cameraFovAngleVertical,
    float cameraNear,
    float cameraFar,
    bool enableSharpening,
    float sharpness )
{
    auto& settings = Engine::GAPI->GetRendererState().RendererSettings;
    const bool rendererMenuActive = Engine::ImGuiHandle && Engine::ImGuiHandle->GetIsActive();
    const bool frameGenerationContextRequested = settings.EnableFrameGeneration
        && settings.AntiAliasingMode == GothicRendererSettings::AA_FSR
        && settings.Upscaler == GothicRendererSettings::UPSCALER_FSR_3
        && !FeatureLevel10Compatibility;
    const bool frameGenerationRuntimeActive = frameGenerationContextRequested
        && !settings.BinkVideoRunning
        && !Engine::GAPI->IsInSavingLoadingState()
        && !Engine::GAPI->IsGamePaused()
        && !rendererMenuActive;

    // Transient menus, loading screens and pauses only suspend dispatch. Keep the
    // expensive combined FSR3 context alive so opening F11 does not destroy and
    // recreate Optical Flow / Frame Interpolation resources twice.
    if ( !Init( inputSize, outputSize, frameGenerationContextRequested ) ) {
        if ( frameGenerationContextRequested ) {
            LogError() << "FSR3: Frame Generation initialization failed; falling back to FSR3 upscaling only.";
            settings.EnableFrameGeneration = false;
            if ( !Init( inputSize, outputSize, false ) ) {
                LogError() << "FSR3: Failed to initialize.";
                return XR_FAILED;
            }
        } else {
            LogError() << "FSR3: Failed to initialize.";
            return XR_FAILED;
        }
    }

    auto* engine = static_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);
    ID3D11DeviceContext* context = engine->GetContext().Get();

    engine->SetDefaultStates();
    engine->UpdateRenderStates();

    ID3D11RenderTargetView* nullRtvs[8] = {};
    ID3D11ShaderResourceView* nullSrvs[16] = {};
    context->OMSetRenderTargets( static_cast<UINT>(std::size( nullRtvs )), nullRtvs, nullptr );
    context->VSSetShaderResources( 0, static_cast<UINT>(std::size( nullSrvs )), nullSrvs );
    context->PSSetShaderResources( 0, static_cast<UINT>(std::size( nullSrvs )), nullSrvs );
    context->CSSetShaderResources( 0, static_cast<UINT>(std::size( nullSrvs )), nullSrvs );

    FfxFsr3DispatchUpscaleDescription dispatch = {};
    dispatch.commandList = ffxGetCommandListDX11( context );
    dispatch.color = WrapView( color, L"FSR3 Input Color" );
    dispatch.depth = WrapView( depth, L"FSR3 Input Depth" );
    dispatch.motionVectors = WrapView( motionVectors, L"FSR3 Input Motion Vectors" );
    dispatch.upscaleOutput = WrapView( output, L"FSR3 Upscale Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS );
    if ( reactiveMask ) {
        dispatch.reactive = WrapView( reactiveMask, L"FSR3 Reactive Mask" );
    }
    if ( transparencyAndCompositionMask ) {
        dispatch.transparencyAndComposition = WrapView( transparencyAndCompositionMask, L"FSR3 Transparency and Composition" );
    }

    dispatch.renderSize = {
        static_cast<uint32_t>(inputSize.x),
        static_cast<uint32_t>(inputSize.y)
    };
    dispatch.upscaleSize = {
        static_cast<uint32_t>(outputSize.x),
        static_cast<uint32_t>(outputSize.y)
    };
    dispatch.jitterOffset = { jitterOffset.x, jitterOffset.y };
    dispatch.motionVectorScale = { motionVectorScale.x, motionVectorScale.y };
    // Keep FSR3 upscaling history independent from frame-generation history.
    // Present() resets frame generation whenever interpolation is inactive or unsafe;
    // feeding that reset into the upscaler would discard FSR3 temporal history every
    // frame and causes strong flickering even when frame interpolation is disabled.
    dispatch.reset = resetAccumulation;
    dispatch.enableSharpening = enableSharpening;
    dispatch.sharpness = std::clamp( sharpness, 0.0f, 1.0f );
    dispatch.frameTimeDelta = std::max( deltaTimeMs, 1.0f );
    dispatch.preExposure = 1.0f;
    dispatch.viewSpaceToMetersFactor = 0.01f;
    dispatch.cameraFovAngleVertical = XMConvertToRadians( cameraFovAngleVertical );
    dispatch.cameraNear = cameraNear;
    dispatch.cameraFar = cameraFar;
    dispatch.frameID = FrameId;

    FfxErrorCode upscaleResult = FFX_ERROR_BACKEND_API_ERROR;
    try {
        upscaleResult = ffxFsr3ContextDispatchUpscale( Context, &dispatch );
    } catch ( ... ) {
        LogError() << "FSR3: DX11 backend exception during upscaling dispatch.";
    }
    if ( upscaleResult != FFX_OK ) {
        LogError() << "FSR3: Upscaling dispatch failed (" << upscaleResult << ").";
        ResetFrameGenerationHistory();
        return XR_FAILED;
    }

    FrameGenerationPrepared = false;
    HudlessCaptured = false;

    if ( ContextFrameGenerationEnabled && frameGenerationRuntimeActive ) {
        FfxFsr3DispatchFrameGenerationPrepareDescription prepare = {};
        prepare.commandList = dispatch.commandList;
        prepare.depth = dispatch.depth;
        prepare.motionVectors = dispatch.motionVectors;
        prepare.jitterOffset = dispatch.jitterOffset;
        prepare.motionVectorScale = dispatch.motionVectorScale;
        prepare.renderSize = dispatch.renderSize;
        prepare.frameTimeDelta = dispatch.frameTimeDelta;
        prepare.cameraNear = dispatch.cameraNear;
        prepare.cameraFar = dispatch.cameraFar;
        prepare.viewSpaceToMetersFactor = dispatch.viewSpaceToMetersFactor;
        prepare.cameraFovAngleVertical = dispatch.cameraFovAngleVertical;
        prepare.frameID = FrameId;

        FfxErrorCode prepareResult = FFX_ERROR_BACKEND_API_ERROR;
        try {
            prepareResult = ffxFsr3ContextDispatchFrameGenerationPrepare( Context, &prepare );
        } catch ( ... ) {
            LogError() << "FSR3: DX11 backend exception during frame-generation prepare.";
        }
        if ( prepareResult == FFX_OK ) {
            UpdateDiagnosticsWindow();
            ++DiagnosticsPreparedFrames;
            PreparedFrameId = FrameId;
            FrameGenerationPrepared = true;
        } else {
            ++Diagnostics.TotalErrors;
            LogError() << "FSR3: Frame-generation prepare failed (" << prepareResult << ").";
            ResetFrameGenerationHistory();
        }
    }

    UnbindComputeResources( context );
    ++FrameId;
    return XR_SUCCESS;
}

void D3D11PFX_FSR3::CaptureHUDLess( ID3D11ShaderResourceView* source ) {
    if ( !Initialized
        || !ContextFrameGenerationEnabled
        || !FrameGenerationPrepared
        || !source
        || !HudlessColor ) {
        return;
    }

    auto* engine = static_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);
    ID3D11DeviceContext* context = engine->GetContext().Get();
    UnbindFrameGenerationInputs( context );

    Renderer->CopyTextureToRTV(
        source,
        HudlessColor->GetRenderTargetView(),
        MaxOutputSize );
    HudlessCaptured = true;
}

ID3D11ShaderResourceView* D3D11PFX_FSR3::GenerateInterpolatedFrame(
    ID3D11ShaderResourceView* presentColor )
{
    if ( !Initialized
        || !ContextFrameGenerationEnabled
        || !FrameGenerationPrepared
        || !HudlessCaptured
        || !presentColor
        || !PresentColor
        || !InterpolatedOutput ) {
        return nullptr;
    }

    UpdateDiagnosticsWindow();

    auto* engine = static_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);
    ID3D11DeviceContext* context = engine->GetContext().Get();
    UnbindFrameGenerationInputs( context );

    Renderer->CopyTextureToRTV(
        presentColor,
        PresentColor->GetRenderTargetView(),
        MaxOutputSize );

    if ( !FrameGenerationConfigured ) {
        FfxFrameGenerationConfig config = {};
        config.frameGenerationEnabled = true;
        config.allowAsyncWorkloads = false;
        config.frameGenerationCallback = &ffxFsr3DispatchFrameGeneration;
        config.HUDLessColor = WrapResource(
            HudlessColor->GetTexture().Get(),
            L"FSR3 HUD-less Color" );

        FfxErrorCode configResult = FFX_ERROR_BACKEND_API_ERROR;
        try {
            configResult = ffxFsr3ConfigureFrameGeneration( Context, &config );
        } catch ( ... ) {
            LogError() << "FSR3: DX11 backend exception during frame-generation configuration.";
        }
        if ( configResult != FFX_OK ) {
            ++Diagnostics.TotalErrors;
            LogError() << "FSR3: Frame-generation configuration failed (" << configResult << ").";
            ResetFrameGenerationHistory();
            return nullptr;
        }
        FrameGenerationConfigured = true;
    }

    ID3D11RenderTargetView* nullRtvs[8] = {};
    context->OMSetRenderTargets( static_cast<UINT>(std::size( nullRtvs )), nullRtvs, nullptr );
    UnbindComputeResources( context );

    const bool reset = ForceFrameGenerationReset || PreparedFrameCount == 0;

    FfxFrameGenerationDispatchDescription dispatch = {};
    dispatch.commandList = ffxGetCommandListDX11( context );
    dispatch.presentColor = WrapResource(
        PresentColor->GetTexture().Get(),
        L"FSR3 Present Color" );
    dispatch.outputs[0] = WrapResource(
        InterpolatedOutput->GetTexture().Get(),
        L"FSR3 Interpolated Output",
        FFX_RESOURCE_STATE_UNORDERED_ACCESS );
    dispatch.numInterpolatedFrames = 1;
    dispatch.reset = reset;
    dispatch.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SCRGB;
    dispatch.minMaxLuminance[0] = 0.0f;
    // The renderer input is already linear. scRGB with an 80-nit range maps
    // 0..1 to 0..1 in FidelityFX without applying an sRGB gamma decode.
    dispatch.minMaxLuminance[1] = 80.0f;
    dispatch.interpolationRect = {
        0, 0,
        static_cast<int32_t>(MaxOutputSize.x),
        static_cast<int32_t>(MaxOutputSize.y)
    };
    dispatch.frameID = PreparedFrameId;

    FfxErrorCode result = FFX_ERROR_BACKEND_API_ERROR;
    try {
        result = ffxFsr3DispatchFrameGeneration( &dispatch );
    } catch ( ... ) {
        LogError() << "FSR3: DX11 backend exception during Optical Flow / Frame Interpolation dispatch.";
    }

    UnbindComputeResources( context );
    engine->SetDefaultStates();
    engine->UpdateRenderStates();

    FrameGenerationPrepared = false;
    HudlessCaptured = false;

    if ( result != FFX_OK ) {
        ++Diagnostics.TotalErrors;
        LogError() << "FSR3: Optical Flow / Frame Interpolation dispatch failed (" << result << ").";
        ForceFrameGenerationReset = true;
        PreparedFrameCount = 0;
        return nullptr;
    }

    const bool canPresentInterpolatedFrame = !reset && PreparedFrameCount > 0;
    PreparedFrameCount = std::min<uint32_t>( PreparedFrameCount + 1, 2 );
    ForceFrameGenerationReset = false;
    if ( canPresentInterpolatedFrame ) {
        ++DiagnosticsGeneratedFrames;
    }

    return canPresentInterpolatedFrame
        ? InterpolatedOutput->GetShaderResView().Get()
        : nullptr;
}

void D3D11PFX_FSR3::UpdateDiagnosticsWindow() {
    const uint64_t now = GetTickCount64();
    if ( DiagnosticsWindowStartMs == 0 ) {
        DiagnosticsWindowStartMs = now;
        return;
    }

    const uint64_t elapsedMs = now - DiagnosticsWindowStartMs;
    if ( elapsedMs < 1000 ) {
        return;
    }

    const float scale = 1000.0f / static_cast<float>( std::max<uint64_t>( elapsedMs, 1 ) );
    Diagnostics.RenderedFps = DiagnosticsRenderedFrames * scale;
    Diagnostics.PreparedFps = DiagnosticsPreparedFrames * scale;
    Diagnostics.GeneratedFps = DiagnosticsGeneratedFrames * scale;
    Diagnostics.PresentedFps = DiagnosticsPresentedFrames * scale;

    if ( ContextFrameGenerationEnabled ) {
        LogInfo() << "FSR3 FG diagnostics: rendered=" << Diagnostics.RenderedFps
            << " prepared=" << Diagnostics.PreparedFps
            << " generated=" << Diagnostics.GeneratedFps
            << " presented=" << Diagnostics.PresentedFps
            << " resets=" << Diagnostics.TotalResets
            << " errors=" << Diagnostics.TotalErrors;
    }

    DiagnosticsRenderedFrames = 0;
    DiagnosticsPreparedFrames = 0;
    DiagnosticsGeneratedFrames = 0;
    DiagnosticsPresentedFrames = 0;
    DiagnosticsWindowStartMs = now;
}

void D3D11PFX_FSR3::NotifyPresent( bool generatedFrame, bool succeeded ) {
    UpdateDiagnosticsWindow();
    if ( succeeded ) {
        ++DiagnosticsPresentedFrames;
        if ( !generatedFrame ) {
            ++DiagnosticsRenderedFrames;
        }
    } else {
        ++Diagnostics.TotalErrors;
    }
}

void D3D11PFX_FSR3::ResetFrameGenerationHistory() {
    const bool hadActiveHistory = FrameGenerationConfigured
        || FrameGenerationPrepared
        || HudlessCaptured
        || PreparedFrameCount > 0;
    if ( hadActiveHistory ) {
        ++Diagnostics.TotalResets;
    }
    FrameGenerationPrepared = false;
    HudlessCaptured = false;
    FrameGenerationConfigured = false;
    ForceFrameGenerationReset = true;
    PreparedFrameCount = 0;
}

#pragma once
#include "pch.h"

#define FFX_OF 1
#define FFX_IF 1
#include <FidelityFX/host/ffx_interface.h>
#include <FidelityFX/host/ffx_fsr3.h>

class D3D11PfxRenderer;
struct RenderToTextureBuffer;

struct FSR3FrameGenerationDiagnostics {
    float RenderedFps = 0.0f;
    float PreparedFps = 0.0f;
    float GeneratedFps = 0.0f;
    float PresentedFps = 0.0f;
    uint64_t TotalResets = 0;
    uint64_t TotalErrors = 0;
};

class D3D11PFX_FSR3 {
public:
    explicit D3D11PFX_FSR3( D3D11PfxRenderer* renderer );
    ~D3D11PFX_FSR3();

    bool Init( const INT2& maxInputSize, const INT2& maxOutputSize, bool enableFrameGeneration );
    void Destroy();

    XRESULT Apply(
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
        float cameraNear = 0.1f,
        float cameraFar = 1000.0f,
        bool enableSharpening = true,
        float sharpness = 0.2f );

    // Capture the upscaled scene before Gothic draws its HUD. Frame interpolation
    // uses this HUD-less image for optical flow while preserving the real HUD.
    void CaptureHUDLess( ID3D11ShaderResourceView* source );

    // Generates one frame between the previous and current rendered frames.
    // Returns nullptr while history is warming up or after a reset.
    ID3D11ShaderResourceView* GenerateInterpolatedFrame( ID3D11ShaderResourceView* presentColor );

    // Used for menus, loading screens, camera discontinuities and mode changes.
    void ResetFrameGenerationHistory();

    bool IsFrameGenerationContextActive() const {
        return Initialized && ContextFrameGenerationEnabled;
    }

    const FSR3FrameGenerationDiagnostics& GetFrameGenerationDiagnostics() const {
        return Diagnostics;
    }

    void NotifyPresent( bool generatedFrame, bool succeeded );

private:
    void UpdateDiagnosticsWindow();

    D3D11PfxRenderer* Renderer;

    FfxInterface Backends[3];
    FfxFsr3Context* Context;
    void* ScratchMemory[3];

    std::unique_ptr<RenderToTextureBuffer> HudlessColor;
    std::unique_ptr<RenderToTextureBuffer> PresentColor;
    std::unique_ptr<RenderToTextureBuffer> InterpolatedOutput;

    INT2 MaxInputSize;
    INT2 MaxOutputSize;
    bool Initialized;
    bool ContextFrameGenerationEnabled;
    bool FrameGenerationPrepared;
    bool HudlessCaptured;
    bool FrameGenerationConfigured;
    bool ForceFrameGenerationReset;
    uint32_t PreparedFrameCount;
    uint64_t FrameId;
    uint64_t PreparedFrameId;

    FSR3FrameGenerationDiagnostics Diagnostics;
    uint64_t DiagnosticsWindowStartMs;
    uint32_t DiagnosticsRenderedFrames;
    uint32_t DiagnosticsPreparedFrames;
    uint32_t DiagnosticsGeneratedFrames;
    uint32_t DiagnosticsPresentedFrames;
};

#include "ImGuiShim.h"
#include "GSky.h"
#include <VersionHelpers.h>
#include <ShellScalingApi.h>

#include "zCParser.h"
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <codecvt>

namespace ImGui {
    void TextUnformatted( const wchar_t* text ) {
        char dest[64];
        auto len = WideCharToMultiByte(CP_UTF8, 0, text, -1, dest, sizeof(dest), NULL, NULL);
        dest[std::min(static_cast<size_t>(len), sizeof(dest) - 1)] = '\0';
        ImGui::TextUnformatted( dest );
    }
}

#if defined(BUILD_GOTHIC_1_08k) && !defined(BUILD_1_12F)
extern bool haveWindAnimations;
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
extern float* ShadowMapLambda;
extern float* ShadowMapBias;

enum class TX_QUALITY : uint16_t {
    VeryLow = 128,
    Low = 256,
    Medium = 512,
    High = 1024,
    VeryHigh = 2048,
    MAX = 16384,
};

int GetDpi( HWND hWnd )
{
    bool v81 = IsWindows8Point1OrGreater();
    bool v10 = IsWindows10OrGreater();

    if ( v81 || v10 ) {

        typedef HRESULT( WINAPI* GetDpiForMonitor_t )(
        HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);

        HMODULE hShcore = LoadLibraryW( L"Shcore.dll" );
        if ( hShcore ) {
            GetDpiForMonitor_t pGetDpiForMonitor = reinterpret_cast<GetDpiForMonitor_t>(GetProcAddress( hShcore, "GetDpiForMonitor" ));
            if ( pGetDpiForMonitor ) {
                HMONITOR hMonitor = ::MonitorFromWindow( hWnd, MONITOR_DEFAULTTONEAREST );
                UINT xdpi, ydpi;
                LRESULT success = pGetDpiForMonitor( hMonitor, MDT_EFFECTIVE_DPI, &xdpi, &ydpi );
                if ( success == S_OK ) {
                    FreeLibrary( hShcore );
                    return static_cast<int>(ydpi);
                }
            }
            FreeLibrary( hShcore );
        }
    }

    // fallback if not available
    HDC hDC = ::GetDC( hWnd );
    INT ydpi = ::GetDeviceCaps( hDC, LOGPIXELSY );
    ::ReleaseDC( NULL, hDC );

    return ydpi;
}

void ApplyFeatureLevel10Downgrades(GothicRendererSettings& s);

void ImGuiShim::Init(
    HWND Window,
    const Microsoft::WRL::ComPtr<ID3D11Device1>& device,
    const Microsoft::WRL::ComPtr<ID3D11DeviceContext1>& context
)
{ 
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; //Not needed and it's annoying.
    OutputWindow = Window;
    ImGui_ImplWin32_Init( OutputWindow );
    ImGui_ImplDX11_Init( device.Get(), context.Get() );

    const auto actualDPI = GetDpi( Window );
    Initiated = true;

    std::vector<DisplayModeInfo> modes;
    Engine::GraphicsEngine->GetDisplayModeList( &modes );
    Resolutions.clear();
    for ( auto it = modes.rbegin(); it != modes.rend(); ++it ) {
        std::string s = std::to_string( (*it).Width ) + "x" + std::to_string( (*it).Height );
        Resolutions.emplace_back( std::make_pair(INT2((*it).Width, (*it).Height), s) );
    }

    //static const ImWchar euroGlyphRanges[] = {
    //    0x0020, 0x007E, // Basic Latin
    //    0x00A0, 0x00FF, // Latin-1 Supplement
    //    0x0100, 0x017F, // Latin Extended-A
    //    0x0180, 0x018F, // Latin Extended-B
    //    0x0400, 0x04FF, // Cyrillic
    //    0x2010, 0x2015, // Various dashes
    //    0x201E, 0x201E, // low-9 quotation mark
    //    0x201C, 0x201D, // high-9 quotation marks
    //    0,              // End of ranges
    //};
    ImFontConfig config = { };
    config.MergeMode = false;
    //config.GlyphRanges = euroGlyphRanges;
    const auto path = std::filesystem::current_path();
    const auto fontpath = path / "system" / "GD3D11" / "Fonts" / "Lato-Semibold.ttf";

    auto dpiScale = actualDPI / 96.0f;
    io.Fonts->AddFontFromFileTTF( fontpath.string().c_str(), 20.0f * dpiScale, &config );}


ImGuiShim::~ImGuiShim()
{
    if ( Initiated ) {
        ImGui_ImplWin32_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void ImGuiShim::RenderLoop()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::GetIO().MouseDrawCursor = GetIsActive() && INT2( ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y ) != Engine::GraphicsEngine->GetResolution();

    static zSTRING GDX_IMGUI_BEGINFRAME = "GDX_IMGUI_BEGINFRAME";
    static zSTRING GDX_IMGUI_ENDFRAME = "GDX_IMGUI_ENDFRAME";
    static int beginFrameFn = zCParser::GetParser()->GetIndex( GDX_IMGUI_BEGINFRAME );
    static int endFrameFn = zCParser::GetParser()->GetIndex( GDX_IMGUI_ENDFRAME );

    static int retryFindFuncs = 0;
    if ( retryFindFuncs > 120 ) {
        if ( beginFrameFn == -1 ) { beginFrameFn = zCParser::GetParser()->GetIndex( GDX_IMGUI_BEGINFRAME ); }
        if ( endFrameFn == -1 ) { endFrameFn = zCParser::GetParser()->GetIndex( GDX_IMGUI_ENDFRAME ); }
        retryFindFuncs = 0;
    }

    LibShowBlockingThisFrame = false;
    LibShowNonBlockingThisFrame = false;
    if ( beginFrameFn != -1 ) {
        zCParser::GetParser()->CallFunc( beginFrameFn );
    } else {
        retryFindFuncs++;
    }

    auto oldSettings = Engine::GAPI->GetRendererState().RendererSettings;
    if ( SettingsVisible ) {
        RenderSettingsWindow();
    } else if ( AdvancedSettingsVisible ) {
        RenderAdvancedSettingsWindow();
    }

    if ( memcmp( &oldSettings, &Engine::GAPI->GetRendererState().RendererSettings, sizeof( GothicRendererSettings ) ) != 0 ) {
        if ( oldSettings.GraphicsPreset == Engine::GAPI->GetRendererState().RendererSettings.GraphicsPreset ) {
            Engine::GAPI->GetRendererState().RendererSettings.GraphicsPreset = GothicRendererSettings::E_GraphicsPreset::GRAPHICS_CUSTOM;
        }
        if ( FeatureLevel10Compatibility ) {
            ApplyFeatureLevel10Downgrades( Engine::GAPI->GetRendererState().RendererSettings );
        }
    }
    //if ( DemoVisible )
    //    ImGui::ShowDemoWindow();

    if ( GetBlockGameInput() != m_lastFrameBlockGameInput ) {
        m_lastFrameBlockGameInput = GetBlockGameInput();
        D3D11GraphicsEngine::UpdateShouldBlockGameInput();
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );

    if ( endFrameFn != -1 ) {
        zCParser::GetParser()->CallFunc( endFrameFn );
    };
}

bool ImGuiShim::GetIsActive() {
    return Initiated && (
        SettingsVisible
        || AdvancedSettingsVisible        || LibShowBlockingThisFrame
        || LibShowNonBlockingThisFrame
    );
}

bool ImGuiShim::GetBlockGameInput()
{
    if ( !GetIsActive() ) {
        return false;
    }
    if ( SettingsVisible
        || AdvancedSettingsVisible
        || LibShowBlockingThisFrame ) {
        return true;
        }
    return false;
}

LRESULT ImGuiShim::OnWindowMessage( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    if ( Initiated && GetIsActive() )
    {
        return ImGui_ImplWin32_WndProcHandler( hWnd, msg, wParam, lParam );
    }
    return 0;
}

void ImGuiShim::OnResize( INT2 newSize )
{
    CurrentResolution = newSize;

    std::vector<DisplayModeInfo> modes;
    Engine::GraphicsEngine->GetDisplayModeList( &modes );
    Resolutions.clear();
    for ( auto it = modes.rbegin(); it != modes.rend(); ++it ) {
        std::string s = std::to_string( (*it).Width ) + "x" + std::to_string( (*it).Height );
        Resolutions.emplace_back( std::make_pair(INT2((*it).Width, (*it).Height), s) );
    }
}

template <typename T>
bool ImComboBoxC( const char* id, const std::vector<std::pair<const char*, T>>& items, T* storage, const std::function<void()>& selected ) {
    if ( storage == nullptr || items.size() == 0 ) {
        return ImGui::BeginCombo( id, "invalid storage" );
    }
    std::pair<const char*, T> selectedItem = items[0];
    for ( auto& it : items ) {
        if ( it.second == *storage ) {
            selectedItem = it;
            break;
        }
    }
    if ( ImGui::BeginCombo( id, selectedItem.first ) ) {
        for ( size_t i = 0; i < items.size(); i++ ) {
            bool isSelected = (*storage == items[i].second);

            if ( ImGui::Selectable( items[i].first, isSelected ) ) {
                *storage = items[i].second;
                selected();
            }

            if ( isSelected ) {
                ImGui::SetItemDefaultFocus();
            }
        }
        return true;
    }
    return false;
}

template <typename T>
bool ImComboBoxCT( const char* id, const std::vector<std::tuple<const char*, T, const char*>>& items, T* storage, const std::function<void()>& selected ) {
    if ( storage == nullptr || items.size() == 0 ) {
        return ImGui::BeginCombo( id, "invalid storage" );
    }
    auto selectedItem = items[0];
    for ( auto& it : items ) {
        if ( std::get<1>( it ) == *storage ) {
            selectedItem = it;
            break;
        }
    }
    if ( ImGui::BeginCombo( id, std::get<0>( selectedItem )) ) {
        for ( size_t i = 0; i < items.size(); i++ ) {
            bool isSelected = (*storage == std::get<1>( items[i] ));

            if ( ImGui::Selectable( std::get<0>( items[i] ), isSelected ) ) {
                *storage = std::get<1>( items[i] );
                selected();
            }
            if ( std::get<2>(items[i]) ) {
                ImGui::SetItemTooltip( "%s", std::get<2>( items[i] ) );
            }

            if ( isSelected ) {
                ImGui::SetItemDefaultFocus();
            }
        }
        return true;
    }
    return false;
}

template <typename T>
bool ImComboBox( const char* id, const std::vector<std::pair<const char*, T>>& items, T* storage ) {
    if ( storage == nullptr || items.size() == 0 ) {
        return ImGui::BeginCombo( id, "invalid storage" );
    }
    std::pair<const char*, T> selectedItem = items[0];
    for ( auto& it : items ) {
        if ( it.second == *storage ) {
            selectedItem = it;
            break;
        }
    }
    if ( ImGui::BeginCombo( id, selectedItem.first ) ) {
        for ( size_t i = 0; i < items.size(); i++ ) {
            bool isSelected = (*storage == items[i].second);

            if ( ImGui::Selectable( items[i].first, isSelected ) ) {
                *storage = items[i].second;
            }

            if ( isSelected ) {
                ImGui::SetItemDefaultFocus();
            }
        }
        return true;
    }
    return false;
}

void ImText( const char* label, const ImVec2& size ) {
    auto& col = ImGui::GetStyleColorVec4( ImGuiCol_::ImGuiCol_Button );

    ImGui::PushStyleColor( ImGuiCol_::ImGuiCol_ButtonActive, col );
    ImGui::PushStyleColor( ImGuiCol_::ImGuiCol_ButtonHovered, col );
    ImGui::PushStyleVarX( ImGuiStyleVar_::ImGuiStyleVar_ButtonTextAlign, 0 );

    ImGui::Button( label, size );
    ImGui::PopStyleVar( 1 );

    ImGui::PopStyleColor( 2 );
}

void ApplyFeatureLevel10Downgrades(GothicRendererSettings& s) {
    // one 4k texture, 1/2 2k textures max.
    s.NumShadowCascades = std::min(s.NumShadowCascades, MAX_CSM_CASCADES);

    if (s.NumShadowCascades >= 2) {
        s.DebugSettings.ShadowCascades.Lambda = D3D11ShadowMap::lambdaBiasTable[s.NumShadowCascades].lambda;
        s.DebugSettings.ShadowCascades.Bias = D3D11ShadowMap::lambdaBiasTable[s.NumShadowCascades].bias;
    }
}

void ApplyGraphicsPresets( GothicRendererSettings& s ) {
    const auto preset = s.GraphicsPreset;
    if ( preset == GothicRendererSettings::E_GraphicsPreset::GRAPHICS_CUSTOM ) {
        return;
    }

    s.EnableSSR = true;
    s.EnableSSS = true;
    s.EnableGodRays = true;
    s.EnableContactShadows = true;
    s.EnableScreenSpaceGI = true;

    switch ( preset ) {
    case GothicRendererSettings::GRAPHICS_LOW:
        s.AntiAliasingMode = GothicRendererSettings::E_AntiAliasingMode::AA_FSR;
        s.Upscaler = GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3;
        s.ResolutionScalePercent = 50;
        s.SharpenFactor = 1.0f;
        s.AoMode = AOMode::AO_NONE;
        s.EnableDoF = false;
        s.WindQuality = GothicRendererSettings::EWindQuality::WIND_QUALITY_NONE;
        s.EnableDynamicLighting = true;
        s.EnablePointlightShadows = GothicRendererSettings::EPointLightShadowMode::PLS_STATIC_ONLY;
        s.OutdoorVobDrawRadius = 30'000.0f;
        s.OutdoorSmallVobDrawRadius = 10'000.0f;
        s.SectionDrawRadius = 1;
        s.VisualFXDrawRadius = 5'000.0f;
        s.textureMaxSize = static_cast<int>(TX_QUALITY::High);
        break;
    case GothicRendererSettings::GRAPHICS_MEDIUM:
        s.AntiAliasingMode = GothicRendererSettings::E_AntiAliasingMode::AA_FSR;
        s.Upscaler = GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3;
        s.ResolutionScalePercent = 75;
        s.SharpenFactor = 1.0f;
        s.AoMode = AOMode::AO_ASSAO;
        s.EnableDoF = true;
        s.WindQuality = GothicRendererSettings::EWindQuality::WIND_QUALITY_ADVANCED;
        s.EnableDynamicLighting = true;
        s.EnablePointlightShadows = GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC;
        s.OutdoorVobDrawRadius = 50'000.0f;
        s.OutdoorSmallVobDrawRadius = 15'000.0f;
        s.SectionDrawRadius = 5;
        s.VisualFXDrawRadius = 10'000.0f;
        s.textureMaxSize = static_cast<int>(TX_QUALITY::MAX);
        break;
    case GothicRendererSettings::GRAPHICS_HIGH:
        s.AntiAliasingMode = GothicRendererSettings::E_AntiAliasingMode::AA_SMAA;
        s.Upscaler = GothicRendererSettings::E_Upscaler::UPSCALER_DEFAULT;
        s.ResolutionScalePercent = 100;
        s.SharpenFactor = 0.2f;
        s.AoMode = AOMode::AO_ASSAO;
        s.EnableDoF = true;
        s.WindQuality = GothicRendererSettings::EWindQuality::WIND_QUALITY_ADVANCED;
        s.EnableDynamicLighting = true;
        s.EnablePointlightShadows = GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC;
        s.OutdoorVobDrawRadius = 75'000.0f;
        s.OutdoorSmallVobDrawRadius = 20'000.0f;
        s.SectionDrawRadius = 10;
        s.VisualFXDrawRadius = 10'000.0f;
        s.textureMaxSize = static_cast<int>(TX_QUALITY::MAX);
        break;
    case GothicRendererSettings::GRAPHICS_VERY_HIGH:
        s.AntiAliasingMode = GothicRendererSettings::E_AntiAliasingMode::AA_SMAA;
        s.Upscaler = GothicRendererSettings::E_Upscaler::UPSCALER_DEFAULT;
        s.ResolutionScalePercent = 100;
        s.SharpenFactor = 0.2f;
        s.AoMode = AOMode::AO_ASSAO;
        s.EnableDoF = true;
        s.WindQuality = GothicRendererSettings::EWindQuality::WIND_QUALITY_ADVANCED;
        s.EnableDynamicLighting = true;
        s.EnablePointlightShadows = GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC;
        s.OutdoorVobDrawRadius = 100'000.0f;
        s.OutdoorSmallVobDrawRadius = 25'000.0f;
        s.SectionDrawRadius = 20;
        s.VisualFXDrawRadius = 10'000.0f;
        s.textureMaxSize = static_cast<int>(TX_QUALITY::MAX);
        break;
    default:
        return;
    }

    if (FeatureLevel10Compatibility) {
        ApplyFeatureLevel10Downgrades(s);
    }

    Engine::GAPI->UpdateTextureMaxSize();
    Engine::GraphicsEngine->ReloadShaders( ShaderCategory::Other );
}
namespace
{
    bool IsFSRUpscaler( GothicRendererSettings::E_Upscaler v ) {
        return v == GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3
            || v == GothicRendererSettings::E_Upscaler::UPSCALER_FSR_2;
    }
    bool UsesTemporalSharpeningBoost( const GothicRendererSettings& s ) {
        return s.AntiAliasingMode == GothicRendererSettings::E_AntiAliasingMode::AA_TAA
            || s.AntiAliasingMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR3
            || (s.AntiAliasingMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR && IsFSRUpscaler( s.Upscaler ));
    }
    void FixupSettings( GothicRendererSettings& s ) {
        s.FixupUpscalingSettings();
    }
}

void ImGuiShim::RenderSettingsWindow()
{
    // Autosized settings by child objects & centered
    IM_ASSERT( ImGui::GetCurrentContext() != NULL && "Missing Dear ImGui context!" );
    IMGUI_CHECKVERSION();

    auto windowSize = CurrentResolution;
    // Get the center point of the screen, then shift the window by 50% of its size in both directions.
    // TIP: Don't use ImGui::GetMainViewport for framebuffer sizes since GD3D11 can undersample or oversample the game.
    // Use whatever the resolution is spit out instead.
    ImVec2 buttonWidth( 275, 0 );
    auto& style = ImGui::GetStyle();

#ifdef IS_DEV_BUILD
    static const char* settingsLabel = "GD3D11 " VERSION_NUMBER " - (" BUILD_DATE ")";
#else
    static const char* settingsLabel = "GD3D11 " VERSION_NUMBER;
#endif

    ShaderCategory shadersToReload = ShaderCategory::None;

    ImGui::SetNextWindowPos( ImVec2( windowSize.x / 2, windowSize.y / 2 ), ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
    if ( ImGui::Begin( settingsLabel, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize ) ) {
        GothicRendererSettings& settings = Engine::GAPI->GetRendererState().RendererSettings;
        FixupSettings(settings);

        static std::vector<std::pair<const char*, int>> graphicsPresets = {
            {"Custom", GothicRendererSettings::E_GraphicsPreset::GRAPHICS_CUSTOM},
            {"Low", GothicRendererSettings::E_GraphicsPreset::GRAPHICS_LOW},
            {"Medium", GothicRendererSettings::E_GraphicsPreset::GRAPHICS_MEDIUM},
            {"High", GothicRendererSettings::E_GraphicsPreset::GRAPHICS_HIGH},
            {"Very High", GothicRendererSettings::E_GraphicsPreset::GRAPHICS_VERY_HIGH},
        };

        ImGui::TextUnformatted("Graphics Preset"); ImGui::SameLine();
        
        ImGui::PushItemWidth( 250 );
        if ( ImComboBoxC( "##GraphicsPreset", graphicsPresets, (int*)&settings.GraphicsPreset, [&settings]() {
            ApplyGraphicsPresets( settings );
            } ) ) {
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        ImGui::Separator();

        const float standardComboWidth = 250.0f;
        const float standardComboStart = std::max( 180.0f,
            ImGui::GetWindowWidth() * 0.5f - standardComboWidth - ImGui::GetStyle().WindowPadding.x );
        
        {
            ImGui::BeginGroup();
            ImGui::Checkbox( "Vsync", &settings.EnableVSync );
            if ( ImGui::Checkbox( "NormalMaps", &settings.AllowNormalmaps ) ) {
                Engine::GAPI->UpdateTextureMaxSize();
            }
            ImGui::BeginDisabled( !settings.AllowNormalmaps );
            ImGui::Checkbox( "Parallax Occlusion Mapping", &settings.EnableParallaxOcclusionMapping );
            ImGui::SetItemTooltip( "Uses *_disp.dds height maps from textures/replacements/Displacementmaps_* folders. Maps are loaded with normalmaps and only used while POM is enabled." );
            ImGui::EndDisabled();

            static std::vector<std::tuple<const char*, AOMode, const char*>> aoModes = {
                    {"Disabled", AOMode::AO_NONE, nullptr},
                    {"HBAO+", AOMode::AO_HBAO, "NVIDIA HBAO+ (Horizon-Based Ambient Occlusion Plus)"},
                    {"SAO", AOMode::AO_SAO, nullptr},
                    {"ASSAO", AOMode::AO_ASSAO, "Intel ASSAO (Adaptive Screen Space Ambient Occlusion)"},
            };
            ImGui::TextUnformatted( "AO Mode" );
            ImGui::SameLine( standardComboStart );
            ImGui::SetNextItemWidth( standardComboWidth );
            if ( ImComboBoxCT( "##AOMode", aoModes, &settings.AoMode, [] {
                Engine::GraphicsEngine->ReloadShaders( ShaderCategory::Other );
                } ) ) {
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip( "Screen-Space ambient occlusion mode.\nChanging this will reload shaders." );
            bool screenSpaceLightFX = settings.EnableContactShadows || settings.EnableScreenSpaceGI;
            if ( ImGui::Checkbox( "Screen-Space Light FX", &screenSpaceLightFX ) ) {
                settings.EnableContactShadows = screenSpaceLightFX;
                settings.EnableScreenSpaceGI = screenSpaceLightFX;
                Engine::GraphicsEngine->ReloadShaders( ShaderCategory::Other );
            }
            ImGui::SetItemTooltip( "Enables contact shadows and indirect screen-space light.\nChanging this will reload shaders." );

            if ( ImGui::Checkbox( "Godrays", &settings.EnableGodRays ) ) {
                Engine::GraphicsEngine->ReloadShaders( ShaderCategory::Other );
            }
            ImGui::SetItemTooltip( "Changing this will reload shaders." );

            bool enhancedWater = settings.EnableSSR;
            if ( ImGui::Checkbox( "Water Effects", &enhancedWater ) ) {
                settings.EnableSSR = enhancedWater;
                settings.EnableWaterAnimation = enhancedWater;
                shadersToReload |= ShaderCategory::Water;
            }
            ImGui::SetItemTooltip( "Enables water reflections, animated waves, and reflections on rain-wet ground." );
            ImGui::Checkbox( "Backlit Vegetation", &settings.EnableSSS );
            ImGui::SetItemTooltip( "Adds soft light transmission to grass, leaves, and alpha-tested vegetation." );
            ImGui::Checkbox( "Depth of Field", &settings.EnableDoF );
            ImGui::SetItemTooltip( "Keeps the subject sharp while softly blurring distant scenery and dialog backgrounds." );

            ImGui::Checkbox( "HDR", &settings.EnableHDR );
            if ( ImGui::Checkbox( "World Shadows", &settings.EnableShadows ) ) {
                shadersToReload |= ShaderCategory::LightsAndShadows;
            }
            ImGui::SetItemTooltip( "Enables cascaded world shadows from the current directional light. Point-light shadows are configured separately." );
            {
                static std::vector<std::pair<const char*, GothicRendererSettings::E_ShadowFilterMode>> shadowFilterModes = {
                    {"Disabled", GothicRendererSettings::E_ShadowFilterMode::SHADOW_FILTER_DISABLED},
                    {"Simple", GothicRendererSettings::E_ShadowFilterMode::SHADOW_FILTER_SIMPLE},
                    {"PCSS", GothicRendererSettings::E_ShadowFilterMode::SHADOW_FILTER_PCSS},
                };
                ImGui::TextUnformatted( "Shadow Filtering" );
                ImGui::SameLine( standardComboStart );
                ImGui::SetNextItemWidth( standardComboWidth );
                if ( ImComboBoxC( "##ShadowFiltering", shadowFilterModes, &settings.ShadowFilterMode, [&shadersToReload]() {
                    shadersToReload |= ShaderCategory::LightsAndShadows;
                    } ) ) {
                    ImGui::EndCombo();
                }
            }



#if defined(BUILD_GOTHIC_2_6_fix) || (defined(BUILD_GOTHIC_1_08k) && !defined(BUILD_1_12F))
#if defined(BUILD_GOTHIC_1_08k) && !defined(BUILD_1_12F)
            if ( haveWindAnimations )
#endif
            {
                bool windEffect = settings.WindQuality != GothicRendererSettings::EWindQuality::WIND_QUALITY_NONE;
                if ( ImGui::Checkbox( "Wind effect", &windEffect ) ) {
                    settings.WindQuality = windEffect
                        ? GothicRendererSettings::EWindQuality::WIND_QUALITY_ADVANCED
                        : GothicRendererSettings::EWindQuality::WIND_QUALITY_NONE;
                    shadersToReload |= ShaderCategory::Other;
                }
                ImGui::SetItemTooltip( "Enables animated wind movement for trees, grass and wheat." );
            }

            if ( ImGui::Checkbox( "Hero affects objects", &settings.HeroAffectsObjects ) ) {
                shadersToReload |= ShaderCategory::Other;
            }
            ImGui::SetItemTooltip( "Grass and wheats may move when the player runs through it." );
#endif //BUILD_GOTHIC_2_6_fix

            ImGui::Checkbox( "Enable Rain", &settings.EnableRain );
            ImGui::SetItemTooltip( "Turns weather particles and wet-ground rain effects on or off." );
            ImGui::Checkbox( "Limit Light Intensity", &settings.LimitLightIntesity );
            ImGui::SetItemTooltip( "Limits overly bright point lights to reduce blown-out interiors." );

            ImGui::EndGroup();
        }

        ImGui::SameLine();

        {
            ImGui::BeginGroup();
            ImGui::PushItemWidth( 250 );

            for (size_t i = 0; i < Resolutions.size(); ++i){
                if (Resolutions[i].first == CurrentResolution) {
                    ResolutionState = i;
                    break;
                }
            }

            static std::string resolutionLabel = "Resolution";

            if ( settings.ResolutionScalePercent != 100 ) {
                std::stringstream ss;
                ss << "Resolution (scaled: " << (CurrentResolution.x * settings.ResolutionScalePercent / 100)
                    << " x "
                    << (CurrentResolution.y * settings.ResolutionScalePercent / 100)
                    << ")";
                resolutionLabel = ss.str();
            }

            ImText( settings.ResolutionScalePercent != 100 ? resolutionLabel.c_str() : "Resolution", buttonWidth ); ImGui::SameLine();
            if ( ImGui::BeginCombo( "##Resolution", Resolutions[ResolutionState].second.c_str() ) ) {
                for ( size_t i = 0; i < Resolutions.size(); i++ ) {
                    bool isSelected = (ResolutionState == i);

                    if ( ImGui::Selectable( Resolutions[i].second.c_str(), isSelected ) ) {
                        Engine::GraphicsEngine->TriggerResize(Resolutions[i].first);
                    }

                    if ( isSelected ) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            static std::vector<std::tuple<const char*, GothicRendererSettings::E_AntiAliasingMode, const char*>> antiAliasing = {
                {"Disabled", GothicRendererSettings::E_AntiAliasingMode::AA_NONE, nullptr },
                {"SMAA", GothicRendererSettings::E_AntiAliasingMode::AA_SMAA, nullptr },
                {"TAA", GothicRendererSettings::E_AntiAliasingMode::AA_TAA, "Temporal Anti-Aliasing" },
                {"FSR 2", GothicRendererSettings::E_AntiAliasingMode::AA_FSR, "FidelityFX Super Resolution 2" },
                {"FSR 3", GothicRendererSettings::E_AntiAliasingMode::AA_FSR3, "FidelityFX Super Resolution 3"},
            };
            {
                ImGui::PushID( "AntiAliasingSettings" );
                auto selectedMode = settings.AntiAliasingMode;
                if ( selectedMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR && settings.Upscaler == GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3 ) {
                    selectedMode = GothicRendererSettings::E_AntiAliasingMode::AA_FSR3;
                }
                ImText( "Anti Aliasing", buttonWidth ); ImGui::SameLine();
                if ( ImComboBoxCT( "##AntiAliasing", antiAliasing, &selectedMode, [&selectedMode, &settings] {
                    if ( selectedMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR3 ) {
                        selectedMode = GothicRendererSettings::E_AntiAliasingMode::AA_FSR;
                        settings.Upscaler = GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3;
                    } else if ( selectedMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR ) {
                        settings.Upscaler = GothicRendererSettings::E_Upscaler::UPSCALER_FSR_2;
                    }
                    settings.AntiAliasingMode = selectedMode;
                    FixupSettings( settings );
                    settings.SharpenFactor = UsesTemporalSharpeningBoost( settings ) ? 1.0f : 0.2f;
                    } ) ) {
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }

            ImText( "Render Scale", buttonWidth ); ImGui::SameLine();
            if ( settings.Upscaler == GothicRendererSettings::UPSCALER_FSR_2 || settings.Upscaler == GothicRendererSettings::UPSCALER_FSR_3 ) {
                settings.ResolutionScalePercent = std::clamp( settings.ResolutionScalePercent, 33, 100 );
                // Display "levels" as typical for FSR
                static std::vector<std::pair<const char*, int>> fsrLevels = {
                    { "Native AA", 100 },
                    { "High Quality", 83 },
                    { "Quality", 75 },
                    { "Balanced", 66 },
                    { "Performance", 50 },
                    { "Ultra Performance", 33 },
                };
                if (ImComboBox( "##ResolutionScalePercent", fsrLevels, &settings.ResolutionScalePercent ) ) {
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("Effective resolution: %d x %d",
                    CurrentResolution.x * settings.ResolutionScalePercent / 100,
                    CurrentResolution.y * settings.ResolutionScalePercent / 100
                );
            } else {
                float resolutionScale = static_cast<float>(settings.ResolutionScalePercent);
                if ( ImGui::SliderFloat( "##ResolutionScalePercent", &resolutionScale, 25.0f, 200.0f, "%.0f%%" ) ) {
                    resolutionScale = std::clamp( resolutionScale, 25.0f, 200.0f );
                    settings.ResolutionScalePercent = static_cast<int>(resolutionScale);
                    FixupSettings( settings );
                }
                ImGui::SetItemTooltip("Effective resolution: %d x %d",
                    CurrentResolution.x * settings.ResolutionScalePercent / 100,
                    CurrentResolution.y * settings.ResolutionScalePercent / 100
                );
            }


            ImText( "Texture Quality", buttonWidth ); ImGui::SameLine();
            static std::vector<std::pair<const char*, int>> QualityOptions = {
                { "Very Low", static_cast<int>(TX_QUALITY::VeryLow) },
                { "Low", static_cast<int>(TX_QUALITY::Low) },
                { "Medium", static_cast<int>(TX_QUALITY::Medium) },
                { "High", static_cast<int>(TX_QUALITY::High) },
                { "Very High", static_cast<int>(TX_QUALITY::VeryHigh) },
                { "Extreme", static_cast<int>(TX_QUALITY::MAX) }, // TODO: this should depend on the GPU capabilities like in the original game
            };
            
            if (settings.textureMaxSize > QualityOptions.back().second) {
                settings.textureMaxSize = QualityOptions.back().second;
                Engine::GAPI->UpdateTextureMaxSize();
            }
            if (settings.textureMaxSize < QualityOptions.front().second) {
                settings.textureMaxSize = QualityOptions.front().second;
                Engine::GAPI->UpdateTextureMaxSize();
            }

            if (ImComboBoxC("##TextureQuality", QualityOptions, &settings.textureMaxSize, []{
                Engine::GAPI->UpdateTextureMaxSize();
            } ))
            {
                ImGui::EndCombo();
            }

            ImText( "Display Mode [*]", buttonWidth );
            ImGui::SetItemTooltip("some changes may require a restart");
            ImGui::SameLine();

            static auto displayModeState = InterpretWindowMode( settings );
            static std::vector<std::tuple<const char*, WindowModes, const char*>> DisplayEnums = {
                { "Fullscreen Borderless", WindowModes::WINDOW_MODE_FULLSCREEN_BORDERLESS, nullptr },
                { "Fullscreen Lowlatency [*]", WindowModes::WINDOW_MODE_FULLSCREEN_LOWLATENCY, "switching requires restarting the game"},
                { "Fullscreen Exclusive [*]", WindowModes::WINDOW_MODE_FULLSCREEN_EXCLUSIVE, "switching requires restarting the game"},
                { "Windowed", WindowModes::WINDOW_MODE_WINDOWED, nullptr},
            };
            
            if ( ImComboBoxCT( "##DisplayMode", DisplayEnums, &displayModeState, [&settings] {
                // selected
                settings.ChangeWindowPreset = displayModeState;
                } ) ) {
                ImGui::EndCombo();
            }

            ImText( "Shadow Quality", buttonWidth ); ImGui::SameLine();

            const static std::vector<std::pair<const char*, int>> shadowMapSizesMax = {
                {"very low", 512},
                {"low", 1024},
                {"medium", 2048},
                {"high", 4096},
                {"very high", 8192},
                {"ultra high", 16384},
            };
            const static std::vector<std::pair<const char*, int>> shadowMapSizesDxFeature10 = {
                {"very low", 512},
                {"low", 1024},
                {"medium", 2048},
                {"high", 4096},
                {"very high", 8192},
            };
            const std::vector<std::pair<const char*, int>>& shadowMapSizes = FeatureLevel10Compatibility
                ? shadowMapSizesDxFeature10
                : shadowMapSizesMax;

            if ( ImComboBoxC( "##ShadowQuality", shadowMapSizes, &settings.ShadowMapSize, [&shadersToReload]{
                shadersToReload |= ShaderCategory::LightsAndShadows;
            } ) ) {
                ImGui::EndCombo();
            }

            ImText( "Shadow Softness", buttonWidth ); ImGui::SameLine();
            ImGui::SliderFloat( "##ShadowSoftness", &settings.ShadowSoftness, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
            ImGui::SetItemTooltip( "Controls world and point-light shadow softness. 1.0 uses the softer default." );

            ImText( "Pointlight Shadows", buttonWidth ); ImGui::SameLine();
            
            const static std::vector<std::tuple<const char*, GothicRendererSettings::EPointLightShadowMode, const char*>> dynamicShadowValues = {
                { "Off", GothicRendererSettings::EPointLightShadowMode::PLS_DISABLED, nullptr },
                { "Static", GothicRendererSettings::EPointLightShadowMode::PLS_STATIC_ONLY, nullptr },
                { "Dynamic Update", GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC, nullptr },
                { "Full", GothicRendererSettings::EPointLightShadowMode::PLS_FULL, "Very expensive. Don't use unless you encounter visual bugs." },
            };

            if ( ImComboBoxCT( "##DynamicShadows", dynamicShadowValues, &settings.EnablePointlightShadows, [] {} ) ) {
                ImGui::EndCombo();
            }

            static bool fpsLimitEnabled = 0;
            fpsLimitEnabled = settings.FpsLimit > 0;

            ImText( "FPS Limit", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable FPS Limit", &fpsLimitEnabled ) ) {
                if ( !fpsLimitEnabled ) {
                    settings.FpsLimit = 0;
                } else {
                    settings.FpsLimit = 60;
                }
            }
            ImGui::SameLine();

            ImGui::BeginDisabled( !fpsLimitEnabled );
            ImGui::SliderInt( "##FPSLimit", &settings.FpsLimit, 10, 300 );
            ImGui::EndDisabled();

            ImText( "Object Draw Distance", buttonWidth ); ImGui::SameLine();
            float objectDrawDistance = settings.OutdoorVobDrawRadius / 1000.0f;
            if ( ImGui::SliderFloat( "##OutdoorVobDrawRadius", &objectDrawDistance, 1.f, 100.0f, "%.0f" ) ) {
                settings.OutdoorVobDrawRadius = static_cast<float>(objectDrawDistance * 1000.0f);
            }

            float smallObjectDrawDistance = settings.OutdoorSmallVobDrawRadius / 1000.0f;
            ImText( "Small Object Draw Distance", buttonWidth ); ImGui::SameLine();
            if ( ImGui::SliderFloat( "##OutdoorSmallVobDrawRadius", &smallObjectDrawDistance, 1.f, 100.0f, "%.0f", ImGuiSliderFlags_::ImGuiSliderFlags_ClampOnInput ) ) {
                settings.OutdoorSmallVobDrawRadius = static_cast<float>(smallObjectDrawDistance * 1000.0f);
            }

            float visualFXDrawDistance = settings.VisualFXDrawRadius / 1000.0f;
            ImText( "VisualFX Draw Distance", buttonWidth ); ImGui::SameLine();
            if ( ImGui::SliderFloat( "##VisualFXDrawRadius", &visualFXDrawDistance, 0.1f, 10.0f, "%.0f", ImGuiSliderFlags_::ImGuiSliderFlags_ClampOnInput ) ) {
                settings.VisualFXDrawRadius = static_cast<float>(visualFXDrawDistance * 1000.0f);
            }
            ImText( "World Draw Distance", buttonWidth ); ImGui::SameLine();
            ImGui::SliderInt( "##SectionDrawRadius", &settings.SectionDrawRadius, 1, 20, "%d", ImGuiSliderFlags_::ImGuiSliderFlags_ClampOnInput );

            ImText( "Contrast", buttonWidth ); ImGui::SameLine();
            ImGui::SliderFloat( "##Contrast", &settings.GammaValue, 0.20f, 2.0f, "%.2f", ImGuiSliderFlags_::ImGuiSliderFlags_ClampOnInput );

            ImText( "Brightness", buttonWidth ); ImGui::SameLine();
            ImGui::SliderFloat( "##Brightness", &settings.BrightnessValue, 0.20f, 2.0f, "%.2f", ImGuiSliderFlags_::ImGuiSliderFlags_ClampOnInput );
            ImGui::PopItemWidth();


            ImGui::Spacing();
            auto availableSize = ImGui::GetWindowSize();
            static const char* advancedSettingsHint = "Advanced settings: CTRL+F11 ";
            auto textSize = ImGui::CalcTextSize( advancedSettingsHint );
            ImGui::SetCursorPos( ImVec2( (availableSize.x - textSize.x) - 15, availableSize.y - textSize.y - 50 ) );
            ImGui::TextUnformatted( advancedSettingsHint );

            ImGui::EndGroup();
        }
        
        auto saved = ImGui::Button( "Save Settings", ImVec2( ImGui::GetContentRegionAvail().x, 30.f ) );
        auto worldSettingsPath = Engine::GAPI->GetLoadedWorldSettingsPath(false);
        const bool isInWorld = !worldSettingsPath.empty();
        const bool hasWorldSettings = Toolbox::FileExists( worldSettingsPath );
        if ( ( ImGui::GetIO().KeyCtrl || hasWorldSettings ) && isInWorld ) {
            ImGui::SetItemTooltip("Save settings to \"%s\"", worldSettingsPath.c_str());
        } else {
            ImGui::SetItemTooltip("Save settings.\nCTRL+Click to save just for the current world.");
        }
        
        if ( saved ) {
            Engine::GraphicsEngine->OnUIEvent( BaseGraphicsEngine::UI_ClosedSettings );
            if ( (ImGui::GetIO().KeyCtrl || hasWorldSettings) && isInWorld ) {
                Engine::GAPI->SaveRendererWorldSettings( settings );
            } else {
                Engine::GAPI->SaveRendererWorldSettings( settings, MENU_SETTINGS_FILE);
            }
            Engine::GAPI->SaveMenuSettings( MENU_SETTINGS_FILE );
        }
    }
    ImGui::End();

    if ( shadersToReload != ShaderCategory::None ) {
        Engine::GraphicsEngine->ReloadShaders( shadersToReload );
    }
}

void ImGuiShim::RenderAdvancedColumn2( GothicRendererSettings& settings, GothicAPI* gapi ) {
    if ( ImGui::Begin( "General", nullptr, ImGuiWindowFlags_NoCollapse ) ) {
#ifdef IS_DEV_BUILD
        ImGui::Text( "Version: %s", VERSION_NUMBER " - (" BUILD_DATE ")" );
#else
        ImGui::Text( "Version: %s", VERSION_NUMBER );
#endif

        if ( ImGui::Button( "Reset Settings", ImVec2( ImGui::GetContentRegionAvail().x, 30.f ) ) ) {
            settings.SetDefault();
            Engine::GraphicsEngine->ReloadShaders( ShaderCategory::All );
        }
        ImGui::SetItemTooltip( "Reset all renderer settings to their defaults." );

        auto worldSettingsPath = Engine::GAPI->GetLoadedWorldSettingsPath(false);
        if (!worldSettingsPath.empty() && Toolbox::FileExists( worldSettingsPath ) ) {
            const bool shouldDelete = ImGui::Button( "Delete World Settings", ImVec2( ImGui::GetContentRegionAvail().x, 30.f ) );
            ImGui::SetItemTooltip("Delete the world-settings file for the current world. Current values will be saved into the global settings file.");
            if ( shouldDelete ) {
                std::error_code ec;
                std::filesystem::remove(worldSettingsPath, ec);
                Engine::GAPI->SaveRendererWorldSettings(settings, MENU_SETTINGS_FILE);
            }
        }

        ImGui::SeparatorText( "Rendering" );
        ImGui::Checkbox( "Animate Static Vobs", &settings.AnimateStaticVobs );
        ImGui::SetItemTooltip( "Updates morph-mesh animations on otherwise static world objects." );
        if ( ImGui::Checkbox( "Compress Backbuffer", &settings.CompressBackBuffer ) ) {
            Engine::GAPI->UpdateCompressBackBuffer();
        }
        ImGui::SetItemTooltip( "Uses a lower-precision HDR backbuffer to reduce memory bandwidth." );
        ImGui::Checkbox( "Occlusion Culling", &settings.EnableOcclusionCulling );
        ImGui::SetItemTooltip( "Experimental previous-frame occlusion culling; may cause delayed object visibility." );
        ImGui::Checkbox( "Sort Render Queue", &settings.SortRenderQueue );
        ImGui::Checkbox( "Draw Threaded", &settings.DrawThreaded );
        ImGui::Checkbox( "Do Z Prepass", &settings.DoZPrepass );
        ImGui::SetItemTooltip("Lightweight depth prepass. It can help low-bandwidth systems and hurt others." );

        ImGui::SeparatorText( "Interface and Camera" );
        ImGui::DragFloat( "Gothic UI Scale", &settings.GothicUIScale, 0.01f, 0.01f, 20.0f, "%.2f" );
        ImGui::DragFloat( "Horizontal FOV", &settings.FOVHoriz, 1.0f, 1.0f, 360.0f, "%.0f" );
        ImGui::DragFloat( "Vertical FOV", &settings.FOVVert, 1.0f, 1.0f, 360.0f, "%.0f" );
        ImGui::Checkbox( "Force FOV", &settings.ForceFOV );
        ImGui::Checkbox( "Fix View Frustum", &settings.FixViewFrustum );

#if defined(BUILD_GOTHIC_2_6_fix) || (defined(BUILD_GOTHIC_1_08k) && !defined(BUILD_1_12F))
#if defined(BUILD_GOTHIC_1_08k) && !defined(BUILD_1_12F)
        if ( haveWindAnimations )
#endif
        {
            ImGui::SeparatorText( "Wind" );
            ImGui::SliderFloat( "Wind Strength", &settings.GlobalWindStrength, 0.1f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
            ImGui::SetItemTooltip( "Normalized wind strength. 1.0 equals the stronger default wind used by this build." );
        }
#endif
    }
    ImGui::End();
}
void RenderAdvancedColumn4( GothicRendererSettings& settings, GothicAPI* gapi ) {
    if ( ImGui::Begin( "Post Processing", nullptr, ImGuiWindowFlags_NoCollapse ) ) {
        ImGui::SeparatorText( "Ambient Occlusion" );
        ImGui::PushID( "AOSettings" );
        if ( settings.AoMode == AOMode::AO_HBAO ) {
            ImGui::DragFloat( "Radius", &settings.HbaoSettings.Radius, 0.01f );
            ImGui::DragFloat( "Meters To View Units", &settings.HbaoSettings.MetersToViewSpaceUnits, 0.01f );
            ImGui::DragFloat( "Power Exponent", &settings.HbaoSettings.PowerExponent, 0.01f, 1.0f, 4.0f, "%.2f", ImGuiSliderFlags_ClampOnInput );
            ImGui::DragFloat( "Bias", &settings.HbaoSettings.Bias, 0.01f, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_ClampOnInput );
            ImGui::Checkbox( "Enable Blur", &settings.HbaoSettings.EnableBlur );
            ImGui::DragFloat( "Blur Sharpness", &settings.HbaoSettings.BlurSharpness, 0.01f );
        } else if ( settings.AoMode == AOMode::AO_SAO ) {
            ImGui::DragFloat( "Radius", &settings.SaoSettings.Radius, 0.01f, 0.1f, 10.0f );
            ImGui::DragFloat( "Bias", &settings.SaoSettings.Bias, 0.001f, 0.0f, 0.1f );
            ImGui::DragFloat( "Intensity", &settings.SaoSettings.Intensity, 0.01f, 0.0f, 10.0f );
            ImGui::SliderInt( "Samples", &settings.SaoSettings.NumSamples, 4, 64 );
            ImGui::DragFloat( "Blur Sharpness", &settings.SaoSettings.BlurSharpness, 0.01f, 0.0f, 16.0f );
        } else if ( settings.AoMode == AOMode::AO_ASSAO ) {
            ImGui::TextUnformatted( "Preset" ); ImGui::SameLine();
            if ( ImGui::Button( "Low" ) ) settings.ApplyAssaoPreset(0);
            ImGui::SameLine();
            if ( ImGui::Button( "High" ) ) settings.ApplyAssaoPreset(1);
            ImGui::SameLine();
            if ( ImGui::Button( "Dark" ) ) settings.ApplyAssaoPreset(2);
            ImGui::SameLine();
            if ( ImGui::Button( "Soft" ) ) settings.ApplyAssaoPreset(3);
            ImGui::DragFloat( "Radius", &settings.AssaoSettings.Radius, 0.01f, 0.0f, 0.0f, "%.2f" );
            ImGui::DragFloat( "Shadow Multiplier", &settings.AssaoSettings.ShadowMultiplier, 0.01f, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_ClampOnInput );
            ImGui::DragFloat( "Shadow Power", &settings.AssaoSettings.ShadowPower, 0.01f, 0.5f, 5.0f, "%.2f", ImGuiSliderFlags_ClampOnInput );
            ImGui::DragFloat( "Shadow Clamp", &settings.AssaoSettings.ShadowClamp, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_ClampOnInput );
            static std::vector<std::pair<const char*, int>> assaoQuality = {
                {"Lowest (-1)", -1}, {"Low (0)", 0}, {"Medium (1)", 1}, {"High (2)", 2}, {"Very High/Adaptive (3)", 3}
            };
            if ( ImComboBox( "Quality Level", assaoQuality, &settings.AssaoSettings.QualityLevel ) ) ImGui::EndCombo();
            ImGui::SliderInt( "Blur Pass Count", &settings.AssaoSettings.BlurPassCount, 0, 6 );
            ImGui::DragFloat( "Sharpness", &settings.AssaoSettings.Sharpness, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_ClampOnInput );
            ImGui::DragFloat( "Detail Shadow Strength", &settings.AssaoSettings.DetailShadowStrength, 0.01f, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_ClampOnInput );
        }
        ImGui::PopID();

        ImGui::SeparatorText( "Parallax Occlusion Mapping" );
        ImGui::BeginDisabled( !settings.AllowNormalmaps || !settings.EnableParallaxOcclusionMapping );
        ImGui::SliderFloat( "POM Strength", &settings.ParallaxOcclusionStrength, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Water Effects" );
        ImGui::BeginDisabled( !settings.EnableSSR );
        ImGui::SliderFloat( "SSR Strength", &settings.SSRStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Screen-Space Light FX" );
        const bool screenSpaceLightFX = settings.EnableContactShadows || settings.EnableScreenSpaceGI;
        ImGui::BeginDisabled( !screenSpaceLightFX );
        bool reloadScreenSpaceLightFX = false;
        reloadScreenSpaceLightFX |= ImGui::SliderFloat( "Contact Shadows", &settings.ContactShadowStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        reloadScreenSpaceLightFX |= ImGui::SliderFloat( "Indirect Light", &settings.ScreenSpaceGIStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        if ( reloadScreenSpaceLightFX ) Engine::GraphicsEngine->ReloadShaders( ShaderCategory::Other );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Particles" );
        ImGui::Checkbox( "Adapt to Scene Lighting", &settings.EnableParticleLighting );
        ImGui::BeginDisabled( !settings.EnableParticleLighting );
        ImGui::SliderFloat( "Lighting Adaptation", &settings.ParticleLightingStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Backlit Vegetation" );
        ImGui::BeginDisabled( !settings.EnableSSS );
        ImGui::SliderFloat( "Intensity", &settings.SSSIntensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Depth of Field" );
        ImGui::BeginDisabled( !settings.EnableDoF );
        ImGui::SliderFloat( "Blur Distance", &settings.DoFFocusDistance, 0.0f, 30000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::SliderFloat( "Focus Range", &settings.DoFFocusRange, 100.0f, 30000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::SliderFloat( "Blur Strength", &settings.DoFBokehRadius, 1.0f, 10.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Sharpening" );
        static std::vector<std::pair<const char*, GothicRendererSettings::E_SharpeningMode>> sharpenModes = {
            {"Disabled", GothicRendererSettings::E_SharpeningMode::SHARPEN_NONE},
            {"Simple", GothicRendererSettings::E_SharpeningMode::SHARPEN_SIMPLE},
            {"CAS", GothicRendererSettings::E_SharpeningMode::SHARPEN_CAS},
        };
        if ( ImComboBox( "Mode", sharpenModes, &settings.SharpeningMode ) ) ImGui::EndCombo();
        ImGui::BeginDisabled( !settings.SharpeningMode );
        ImGui::DragFloat( "Factor", &settings.SharpenFactor, 0.001f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::EndDisabled();
    }
    ImGui::End();
}
void ImGuiShim::RenderAdvancedSettingsWindow()
{
    IM_ASSERT( ImGui::GetCurrentContext() != NULL && "Missing Dear ImGui context!" );
    IMGUI_CHECKVERSION();

    auto windowSize = CurrentResolution;
    int numCols = 2;
    auto columnWidth = static_cast<float>(windowSize.x) / numCols;
    auto columnOffset = 0.0f;
    auto columnHeight = std::max( 400.0f, static_cast<float>(windowSize.y) / 2.f );

    GothicRendererSettings& settings = Engine::GAPI->GetRendererState().RendererSettings;
    FixupSettings(settings);

    ImGui::SetNextWindowPos( ImVec2( columnOffset, 0.0f ), ImGuiCond_Appearing, ImVec2( 0, 0 ) );
    ImGui::SetNextWindowSize( ImVec2( columnWidth, columnHeight ), ImGuiCond_Appearing );
    RenderAdvancedColumn2( settings, Engine::GAPI );
    columnOffset += columnWidth;

    ImGui::SetNextWindowPos( ImVec2( columnOffset, 0.0f ), ImGuiCond_Appearing, ImVec2( 0, 0 ) );
    ImGui::SetNextWindowSize( ImVec2( columnWidth, columnHeight ), ImGuiCond_Appearing );
    RenderAdvancedColumn4( settings, Engine::GAPI );
}

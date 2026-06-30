#include "ImGuiShim.h"
#include "GSky.h"
#include <VersionHelpers.h>
#include <ShellScalingApi.h>

#include "zCParser.h"
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>
#include <codecvt>
#include <cstdio>

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

namespace {
    void ApplyGD3D11DarkStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 3.0f;
        style.ChildRounding = 2.0f;
        style.FrameRounding = 2.0f;
        style.PopupRounding = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding = 2.0f;
        style.FrameBorderSize = 0.0f;
        style.WindowBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4( 0.92f, 0.93f, 0.94f, 1.00f );
        colors[ImGuiCol_TextDisabled] = ImVec4( 0.48f, 0.50f, 0.53f, 1.00f );
        colors[ImGuiCol_WindowBg] = ImVec4( 0.045f, 0.048f, 0.052f, 0.94f );
        colors[ImGuiCol_ChildBg] = ImVec4( 0.055f, 0.058f, 0.064f, 0.92f );
        colors[ImGuiCol_PopupBg] = ImVec4( 0.070f, 0.074f, 0.082f, 0.98f );
        colors[ImGuiCol_Border] = ImVec4( 0.25f, 0.26f, 0.28f, 0.70f );
        colors[ImGuiCol_BorderShadow] = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
        colors[ImGuiCol_FrameBg] = ImVec4( 0.115f, 0.122f, 0.135f, 0.94f );
        colors[ImGuiCol_FrameBgHovered] = ImVec4( 0.165f, 0.174f, 0.192f, 1.00f );
        colors[ImGuiCol_FrameBgActive] = ImVec4( 0.205f, 0.216f, 0.238f, 1.00f );
        colors[ImGuiCol_TitleBg] = ImVec4( 0.080f, 0.086f, 0.096f, 1.00f );
        colors[ImGuiCol_TitleBgActive] = ImVec4( 0.115f, 0.124f, 0.138f, 1.00f );
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4( 0.045f, 0.048f, 0.052f, 0.90f );
        colors[ImGuiCol_MenuBarBg] = ImVec4( 0.095f, 0.102f, 0.113f, 1.00f );
        colors[ImGuiCol_ScrollbarBg] = ImVec4( 0.045f, 0.048f, 0.052f, 0.80f );
        colors[ImGuiCol_ScrollbarGrab] = ImVec4( 0.28f, 0.30f, 0.33f, 1.00f );
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4( 0.36f, 0.38f, 0.42f, 1.00f );
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4( 0.48f, 0.50f, 0.55f, 1.00f );
        colors[ImGuiCol_CheckMark] = ImVec4( 0.62f, 0.72f, 0.84f, 1.00f );
        colors[ImGuiCol_SliderGrab] = ImVec4( 0.42f, 0.52f, 0.65f, 1.00f );
        colors[ImGuiCol_SliderGrabActive] = ImVec4( 0.62f, 0.72f, 0.84f, 1.00f );
        colors[ImGuiCol_Button] = ImVec4( 0.145f, 0.154f, 0.170f, 0.94f );
        colors[ImGuiCol_ButtonHovered] = ImVec4( 0.205f, 0.218f, 0.240f, 1.00f );
        colors[ImGuiCol_ButtonActive] = ImVec4( 0.245f, 0.260f, 0.286f, 1.00f );
        colors[ImGuiCol_Header] = ImVec4( 0.150f, 0.160f, 0.178f, 0.94f );
        colors[ImGuiCol_HeaderHovered] = ImVec4( 0.205f, 0.218f, 0.240f, 1.00f );
        colors[ImGuiCol_HeaderActive] = ImVec4( 0.245f, 0.260f, 0.286f, 1.00f );
        colors[ImGuiCol_Separator] = ImVec4( 0.25f, 0.26f, 0.28f, 0.75f );
        colors[ImGuiCol_SeparatorHovered] = ImVec4( 0.42f, 0.52f, 0.65f, 1.00f );
        colors[ImGuiCol_SeparatorActive] = ImVec4( 0.62f, 0.72f, 0.84f, 1.00f );
        colors[ImGuiCol_ResizeGrip] = ImVec4( 0.42f, 0.52f, 0.65f, 0.45f );
        colors[ImGuiCol_ResizeGripHovered] = ImVec4( 0.62f, 0.72f, 0.84f, 0.75f );
        colors[ImGuiCol_ResizeGripActive] = ImVec4( 0.62f, 0.72f, 0.84f, 1.00f );
        colors[ImGuiCol_Tab] = ImVec4( 0.115f, 0.124f, 0.138f, 1.00f );
        colors[ImGuiCol_TabHovered] = ImVec4( 0.205f, 0.218f, 0.240f, 1.00f );
        colors[ImGuiCol_TabActive] = ImVec4( 0.165f, 0.176f, 0.195f, 1.00f );
        colors[ImGuiCol_TabUnfocused] = ImVec4( 0.080f, 0.086f, 0.096f, 1.00f );
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4( 0.125f, 0.134f, 0.150f, 1.00f );
        colors[ImGuiCol_TextSelectedBg] = ImVec4( 0.42f, 0.52f, 0.65f, 0.35f );
        colors[ImGuiCol_NavHighlight] = ImVec4( 0.62f, 0.72f, 0.84f, 0.70f );
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4( 0.92f, 0.93f, 0.94f, 0.70f );
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4( 0.00f, 0.00f, 0.00f, 0.45f );
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4( 0.00f, 0.00f, 0.00f, 0.55f );
    }
    int FindNearestStepIndex( float value, const float* levels, int levelCount )
    {
        int bestIndex = 0;
        float bestDistance = fabsf( value - levels[0] );
        for ( int i = 1; i < levelCount; ++i ) {
            const float distance = fabsf( value - levels[i] );
            if ( distance < bestDistance ) {
                bestIndex = i;
                bestDistance = distance;
            }
        }
        return bestIndex;
    }

    bool SliderSteppedIndex(
        const char* label,
        int* index,
        int maximumIndex,
        bool drawTicks,
        int emphasizedTick = -1,
        const char* displayText = nullptr )
    {
        *index = std::clamp( *index, 0, maximumIndex );

        // Let ImGui handle mouse, keyboard and gamepad interaction with an integer
        // slider. Integer indices make the grab jump to real steps while dragging.
        ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_SliderGrab, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, ImVec4( 0, 0, 0, 0 ) );
        const bool changed = ImGui::SliderInt(
            label, index, 0, maximumIndex, "", ImGuiSliderFlags_AlwaysClamp );
        ImGui::PopStyleColor( 5 );

        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float width = itemMax.x - itemMin.x;
        const float height = itemMax.y - itemMin.y;
        if ( width <= 0.0f || height <= 0.0f || maximumIndex <= 0 ) {
            return changed;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        const ImU32 frameColor = ImGui::GetColorU32(
            active ? ImGuiCol_FrameBgActive : (hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg) );
        drawList->AddRectFilled( itemMin, itemMax, frameColor, style.FrameRounding );
        if ( style.FrameBorderSize > 0.0f ) {
            drawList->AddRect(
                itemMin, itemMax, ImGui::GetColorU32( ImGuiCol_Border ),
                style.FrameRounding, style.FrameBorderSize );
        }

        const float grabPadding = 2.0f;
        const float sliderSize = std::max( 0.0f, width - grabPadding * 2.0f );
        const float grabSize = std::min(
            sliderSize, std::max( style.GrabMinSize, sliderSize / static_cast<float>(maximumIndex + 1) ) );
        const float usableWidth = std::max( 0.0f, sliderSize - grabSize );
        const float firstX = itemMin.x + grabPadding + grabSize * 0.5f;
        const float centerY = (itemMin.y + itemMax.y) * 0.5f;

        if ( drawTicks ) {
            const ImU32 minorTick = ImGui::GetColorU32( ImGuiCol_TextDisabled, 0.65f );
            const ImU32 emphasizedShadow = ImGui::GetColorU32( ImVec4( 0.0f, 0.0f, 0.0f, 0.85f ) );
            const ImU32 emphasizedColor = ImGui::GetColorU32( ImGuiCol_CheckMark );
            for ( int tick = 0; tick <= maximumIndex; ++tick ) {
                const float x = firstX + usableWidth * (static_cast<float>(tick) / maximumIndex);
                if ( tick == emphasizedTick ) {
                    drawList->AddLine(
                        ImVec2( x + 1.0f, itemMin.y + 2.0f ),
                        ImVec2( x + 1.0f, itemMax.y - 2.0f ), emphasizedShadow, 3.0f );
                    drawList->AddLine(
                        ImVec2( x, itemMin.y + 2.0f ),
                        ImVec2( x, itemMax.y - 2.0f ), emphasizedColor, 2.0f );
                } else {
                    drawList->AddLine(
                        ImVec2( x, centerY - 3.0f ),
                        ImVec2( x, centerY + 3.0f ), minorTick, 1.0f );
                }
            }
        }

        // Draw the grab last so every tick remains behind it.
        const float grabCenterX = firstX + usableWidth * (static_cast<float>(*index) / maximumIndex);
        drawList->AddRectFilled(
            ImVec2( grabCenterX - grabSize * 0.5f, itemMin.y + grabPadding ),
            ImVec2( grabCenterX + grabSize * 0.5f, itemMax.y - grabPadding ),
            ImGui::GetColorU32( active ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab ),
            style.GrabRounding );

        if ( displayText && displayText[0] != '\0' ) {
            const ImVec2 textSize = ImGui::CalcTextSize( displayText );
            drawList->AddText(
                ImVec2( itemMin.x + (width - textSize.x) * 0.5f,
                    itemMin.y + (height - textSize.y) * 0.5f ),
                ImGui::GetColorU32( ImGuiCol_Text ), displayText );
        }

        return changed;
    }

    bool SliderNormalizedUiStrength( const char* label, float* value, float minimum = 0.01f, const char* = "" )
    {
        const std::array<float, 11> levels = {
            minimum, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f,
            1.2f, 1.4f, 1.6f, 1.8f, 2.0f
        };
        int index = FindNearestStepIndex( *value, levels.data(), static_cast<int>(levels.size()) );
        *value = levels[index];
        if ( SliderSteppedIndex( label, &index, 10, true, 5 ) ) {
            *value = levels[index];
            return true;
        }
        return false;
    }

    bool SliderNearDoFStrength( const char* label, float* value )
    {
        const std::array<float, 11> levels = {
            0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f,
            3.0f, 3.5f, 4.0f, 4.5f, 5.0f
        };
        int index = FindNearestStepIndex( *value, levels.data(), static_cast<int>(levels.size()) );
        *value = levels[index];
        if ( SliderSteppedIndex( label, &index, 10, true, 2 ) ) {
            *value = levels[index];
            return true;
        }
        return false;
    }

    int SnapRenderScalePercentNonFSR( int value )
    {
        const int clamped = std::clamp( value, 100, 200 );
        return 100 + ((clamped - 100 + 2) / 5) * 5;
    }

    bool SliderRenderScalePercentNonFSR( const char* label, int* value )
    {
        *value = SnapRenderScalePercentNonFSR( *value );
        int index = (*value - 100) / 5;
        char display[16];
        std::snprintf( display, sizeof(display), "%d%%", *value );
        if ( SliderSteppedIndex( label, &index, 20, false, 0, display ) ) {
            *value = 100 + index * 5;
            return true;
        }
        return false;
    }

    float SnapDisplayTuningStrength( float value )
    {
        const float clamped = std::clamp( value, 0.0f, 2.0f );
        const int step = std::clamp( static_cast<int>(clamped * 20.0f + 0.5f), 0, 40 );
        return static_cast<float>(step) * 0.05f;
    }

    bool SliderDisplayTuningStrength( const char* label, float* value )
    {
        *value = SnapDisplayTuningStrength( *value );
        int index = std::clamp( static_cast<int>(*value * 20.0f + 0.5f), 0, 40 );
        char display[16];
        std::snprintf( display, sizeof(display), "%.2f", *value );
        if ( SliderSteppedIndex( label, &index, 40, false, 20, display ) ) {
            *value = static_cast<float>(index) * 0.05f;
            return true;
        }
        return false;
    }
}

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
    ApplyGD3D11DarkStyle();
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

    s.EnableFrameGeneration = false;
    if ( s.AoMode == AOMode::AO_XEGTAO ) {
        s.AoMode = AOMode::AO_ASSAO;
    }

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
    s.EnableFrameGeneration = false;

    switch ( preset ) {
    case GothicRendererSettings::GRAPHICS_LOW:
        s.AntiAliasingMode = GothicRendererSettings::E_AntiAliasingMode::AA_FSR;
        s.Upscaler = GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3;
        s.ResolutionScalePercent = 50;
        s.SharpenFactor = 1.0f;
        s.AoMode = AOMode::AO_NONE;
        s.EnableDoF = false;
        s.WindQuality = GothicRendererSettings::EWindQuality::WIND_QUALITY_NONE;
        s.EnablePointlightShadows = GothicRendererSettings::EPointLightShadowMode::PLS_STATIC_ONLY;
        s.OutdoorSmallVobDrawRadius = 10'000.0f;
        s.SectionDrawRadius = 1;
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
        s.EnablePointlightShadows = GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC;
        s.OutdoorSmallVobDrawRadius = 15'000.0f;
        s.SectionDrawRadius = 5;
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
        s.EnablePointlightShadows = GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC;
        s.OutdoorSmallVobDrawRadius = 20'000.0f;
        s.SectionDrawRadius = 10;
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
        s.EnablePointlightShadows = GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC;
        s.OutdoorSmallVobDrawRadius = 25'000.0f;
        s.SectionDrawRadius = 20;
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
        return v == GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3;
    }
    bool UsesTemporalSharpeningBoost( const GothicRendererSettings& s ) {
        return s.AntiAliasingMode == GothicRendererSettings::E_AntiAliasingMode::AA_TAA
            || s.AntiAliasingMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR3
            || (s.AntiAliasingMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR && IsFSRUpscaler( s.Upscaler ));
    }
    void FixupSettings( GothicRendererSettings& s ) {
        s.FixupUpscalingSettings();
        s.EnableFrameGeneration = false;
    }
}

void ImGuiShim::BeginSettingsEdit() {
    if ( m_settingsEditActive ) {
        return;
    }

    m_settingsSnapshot = Engine::GAPI->GetRendererState().RendererSettings;
    m_settingsResolutionSnapshot = CurrentResolution;
    m_settingsEditActive = true;
}

void ImGuiShim::CommitSettingsEdit() {
    m_settingsEditActive = false;
}

void ImGuiShim::CancelSettingsEdit() {
    if ( !m_settingsEditActive ) {
        return;
    }

    auto& settings = Engine::GAPI->GetRendererState().RendererSettings;
    const bool textureQualityChanged = settings.textureMaxSize != m_settingsSnapshot.textureMaxSize;
    settings = m_settingsSnapshot;
    FixupSettings( settings );
    m_settingsEditActive = false;

    if ( textureQualityChanged ) {
        Engine::GAPI->UpdateTextureMaxSize();
    }
    Engine::GraphicsEngine->TriggerResize( m_settingsResolutionSnapshot );
    Engine::GraphicsEngine->ReloadShaders( ShaderCategory::All );
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
        ImGui::SetItemTooltip( "Applies a balanced group of visible F11 settings. Custom means one or more values were adjusted manually." );
        ImGui::PopItemWidth();
        ImGui::Separator();

        const float standardComboWidth = 250.0f;
        // All right-column value controls start at the same x position.
        const float inlineToggleWidth = (buttonWidth.x - style.ItemSpacing.x) * 0.5f;
        const float inlineToggleLabelWidth = inlineToggleWidth - ImGui::GetFrameHeight() - style.ItemSpacing.x;
        const float compactComboWidth = inlineToggleWidth;
        const float aoModeLabelWidth = inlineToggleWidth;
        
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
            ImGui::SetItemTooltip( "Selects the output resolution. Render Scale controls the internal render resolution separately." );

            static std::vector<std::tuple<const char*, GothicRendererSettings::E_AntiAliasingMode, const char*>> antiAliasing = {
                {"Disabled", GothicRendererSettings::E_AntiAliasingMode::AA_NONE, nullptr },
                {"SMAA", GothicRendererSettings::E_AntiAliasingMode::AA_SMAA, nullptr },
                {"TAA", GothicRendererSettings::E_AntiAliasingMode::AA_TAA, "Temporal Anti-Aliasing" },
                {"FSR 3", GothicRendererSettings::E_AntiAliasingMode::AA_FSR3, "FidelityFX Super Resolution 3"},
            };
            {
                ImGui::PushID( "AntiAliasingSettings" );
                auto selectedMode = settings.AntiAliasingMode;
                if ( selectedMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR ) {
                    selectedMode = GothicRendererSettings::E_AntiAliasingMode::AA_FSR3;
                }
                const bool wasFSRAntiAliasing = settings.AntiAliasingMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR;
                ImText( "Anti Aliasing", buttonWidth ); ImGui::SameLine();
                if ( ImComboBoxCT( "##AntiAliasing", antiAliasing, &selectedMode, [&selectedMode, &settings, wasFSRAntiAliasing] {
                    const bool selectsFSRAntiAliasing = selectedMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR3;
                    if ( wasFSRAntiAliasing && !selectsFSRAntiAliasing ) {
                        settings.ResolutionScalePercent = 100;
                    }

                    if ( selectedMode == GothicRendererSettings::E_AntiAliasingMode::AA_FSR3 ) {
                        selectedMode = GothicRendererSettings::E_AntiAliasingMode::AA_FSR;
                        settings.Upscaler = GothicRendererSettings::E_Upscaler::UPSCALER_FSR_3;
                    }
                    settings.AntiAliasingMode = selectedMode;
                    FixupSettings( settings );
                    settings.SharpenFactor = UsesTemporalSharpeningBoost( settings ) ? 1.0f : 0.2f;
                    } ) ) {
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip( "Selects edge smoothing. FSR 3 also uses Render Scale for its quality presets." );
                ImGui::PopID();
            }
            settings.EnableFrameGeneration = false;
            ImText( "Frame Generation", buttonWidth ); ImGui::SameLine();
            ImGui::BeginDisabled( true );
            ImGui::Checkbox( "##Enable Frame Generation", &settings.EnableFrameGeneration );
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Disabled for now: the DX11 frame-interpolation path is not stable enough for this build." );
            ImText( "Render Scale", buttonWidth ); ImGui::SameLine();
            if ( settings.Upscaler == GothicRendererSettings::UPSCALER_FSR_3 ) {
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
                ImGui::SetItemTooltip("FSR 3 quality preset. Effective resolution: %d x %d",
                    CurrentResolution.x * settings.ResolutionScalePercent / 100,
                    CurrentResolution.y * settings.ResolutionScalePercent / 100
                );
            } else {
                int resolutionScale = SnapRenderScalePercentNonFSR( settings.ResolutionScalePercent );
                if ( resolutionScale != settings.ResolutionScalePercent ) {
                    settings.ResolutionScalePercent = resolutionScale;
                    FixupSettings( settings );
                }
                if ( SliderRenderScalePercentNonFSR( "##ResolutionScalePercent", &resolutionScale ) ) {
                    settings.ResolutionScalePercent = resolutionScale;
                    FixupSettings( settings );
                }
                ImGui::SetItemTooltip("Internal render resolution. Effective resolution: %d x %d",
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
            ImGui::SetItemTooltip( "Limits maximum texture resolution. Lower values reduce VRAM use but make textures blurrier." );

            ImText( "Display Mode [*]", buttonWidth );
            ImGui::SetItemTooltip( "Selects fullscreen or window mode. Entries marked with [*] take effect after restarting the game." );
            ImGui::SameLine();

            static auto displayModeState = InterpretWindowMode( settings );
            static std::vector<std::tuple<const char*, WindowModes, const char*>> DisplayEnums = {
                { "Fullscreen Borderless", WindowModes::WINDOW_MODE_FULLSCREEN_BORDERLESS, nullptr },
                { "Fullscreen Lowlatency [*]", WindowModes::WINDOW_MODE_FULLSCREEN_LOWLATENCY, "This mode takes effect after restarting the game."},
                { "Fullscreen Exclusive [*]", WindowModes::WINDOW_MODE_FULLSCREEN_EXCLUSIVE, "This mode takes effect after restarting the game."},
                { "Windowed", WindowModes::WINDOW_MODE_WINDOWED, nullptr},
            };
            
            if ( ImComboBoxCT( "##DisplayMode", DisplayEnums, &displayModeState, [&settings] {
                // selected
                settings.ChangeWindowPreset = displayModeState;
                } ) ) {
                ImGui::EndCombo();
            }

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

            ImText( "World Shadows", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable World Shadows", &settings.EnableShadows ) ) {
                shadersToReload |= ShaderCategory::LightsAndShadows;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( !settings.EnableShadows );
            if ( ImComboBoxC( "##WorldShadowQuality", shadowMapSizes, &settings.ShadowMapSize, [&shadersToReload]{
                shadersToReload |= ShaderCategory::LightsAndShadows;
            } ) ) {
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Enables sun and moon world shadows and selects their cascaded shadow-map resolution." );

            static std::vector<std::pair<const char*, GothicRendererSettings::E_ShadowFilterMode>> shadowFilterModes = {
                {"Disabled", GothicRendererSettings::E_ShadowFilterMode::SHADOW_FILTER_DISABLED},
                {"Simple", GothicRendererSettings::E_ShadowFilterMode::SHADOW_FILTER_SIMPLE},
                {"PCSS", GothicRendererSettings::E_ShadowFilterMode::SHADOW_FILTER_PCSS},
            };
            ImText( "Shadow Filtering", buttonWidth ); ImGui::SameLine();
            ImGui::BeginDisabled( !settings.EnableShadows );
            if ( ImComboBoxC( "##ShadowFiltering", shadowFilterModes, &settings.ShadowFilterMode, [&shadersToReload]() {
                shadersToReload |= ShaderCategory::LightsAndShadows;
                } ) ) {
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Selects the filtering method for sun and moon world shadows." );

            static GothicRendererSettings::EPointLightShadowMode lastEnabledPointlightShadowMode =
                GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC;
            if ( settings.EnablePointlightShadows != GothicRendererSettings::EPointLightShadowMode::PLS_DISABLED ) {
                lastEnabledPointlightShadowMode = settings.EnablePointlightShadows;
            }
            bool pointlightShadowsEnabled = settings.EnablePointlightShadows != GothicRendererSettings::EPointLightShadowMode::PLS_DISABLED;

            ImText( "Pointlight Shadows", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable Pointlight Shadows", &pointlightShadowsEnabled ) ) {
                settings.EnablePointlightShadows = pointlightShadowsEnabled
                    ? lastEnabledPointlightShadowMode
                    : GothicRendererSettings::EPointLightShadowMode::PLS_DISABLED;
            }
            ImGui::SameLine();

            const static std::vector<std::tuple<const char*, GothicRendererSettings::EPointLightShadowMode, const char*>> pointlightShadowModes = {
                { "Static", GothicRendererSettings::EPointLightShadowMode::PLS_STATIC_ONLY, nullptr },
                { "Dynamic Update", GothicRendererSettings::EPointLightShadowMode::PLS_UPDATE_DYNAMIC, nullptr },
                { "Full", GothicRendererSettings::EPointLightShadowMode::PLS_FULL, "Very expensive. Don't use unless you encounter visual bugs." },
            };
            auto displayedPointlightShadowMode = pointlightShadowsEnabled
                ? settings.EnablePointlightShadows
                : lastEnabledPointlightShadowMode;
            ImGui::BeginDisabled( !pointlightShadowsEnabled );
            if ( ImComboBoxCT( "##PointlightShadowMode", pointlightShadowModes, &displayedPointlightShadowMode, [&settings, &displayedPointlightShadowMode] {
                settings.EnablePointlightShadows = displayedPointlightShadowMode;
                lastEnabledPointlightShadowMode = displayedPointlightShadowMode;
                } ) ) {
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Enables point-light shadows and selects how their cubemaps are updated." );

            ImText( "Shadow Softness", buttonWidth ); ImGui::SameLine();
            SliderNormalizedUiStrength( "##ShadowSoftness", &settings.ShadowSoftness );
            ImGui::SetItemTooltip( "Controls world and point-light shadow softness. 1.0 uses the softer default." );


            float objectDrawDistance = settings.OutdoorSmallVobDrawRadius / 1000.0f;
            ImText( "Object Draw Distance", buttonWidth ); ImGui::SameLine();
            if ( ImGui::SliderFloat( "##OutdoorSmallVobDrawRadius", &objectDrawDistance, 1.f, 100.0f, "%.0f", ImGuiSliderFlags_::ImGuiSliderFlags_ClampOnInput ) ) {
                settings.OutdoorSmallVobDrawRadius = static_cast<float>(objectDrawDistance * 1000.0f);
            }
            ImGui::SetItemTooltip( "Controls the draw distance of grass, vegetation, clutter, and other small objects. This can have a significant performance impact." );

            ImText( "World Draw Distance", buttonWidth ); ImGui::SameLine();
            ImGui::SliderInt( "##SectionDrawRadius", &settings.SectionDrawRadius, 1, 20, "%d", ImGuiSliderFlags_::ImGuiSliderFlags_ClampOnInput );
            ImGui::SetItemTooltip( "Controls how far world sections are drawn. Higher values show more distant terrain and buildings." );

            ImText( "Contrast", buttonWidth ); ImGui::SameLine();
            SliderDisplayTuningStrength( "##Contrast", &settings.GammaValue );
            ImGui::SetItemTooltip( "Adjusts display contrast after rendering." );

            ImText( "Brightness", buttonWidth ); ImGui::SameLine();
            SliderDisplayTuningStrength( "##Brightness", &settings.BrightnessValue );
            ImGui::SetItemTooltip( "Adjusts display brightness after rendering." );
            ImGui::PopItemWidth();

            ImGui::EndGroup();
        }

        ImGui::SameLine();

        {
            ImGui::BeginGroup();
            ImText( "VSync", { inlineToggleLabelWidth, buttonWidth.y } ); ImGui::SameLine();
            ImGui::Checkbox( "##Enable VSync", &settings.EnableVSync );
            ImGui::SameLine();

            bool fpsLimitEnabled = settings.FpsLimit > 0;
            ImGui::BeginDisabled( settings.EnableVSync );
            ImText( "FPS Limit", { inlineToggleLabelWidth, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable FPS Limit", &fpsLimitEnabled ) ) {
                settings.FpsLimit = fpsLimitEnabled ? 60 : 0;
            }
            ImGui::SameLine();

            int inactiveFpsLimit = settings.FpsLimit > 0 ? settings.FpsLimit : 60;
            int* displayedFpsLimit = fpsLimitEnabled ? &settings.FpsLimit : &inactiveFpsLimit;
            ImGui::BeginDisabled( !fpsLimitEnabled );
            ImGui::SetNextItemWidth( standardComboWidth );
            ImGui::SliderInt( "##FPSLimit", displayedFpsLimit, 10, 300,
                settings.EnableVSync ? "Inactive (VSync)" : (fpsLimitEnabled ? "%d FPS" : "Off"),
                ImGuiSliderFlags_AlwaysClamp );
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( settings.EnableVSync
                ? "VSync is active, so frame pacing follows the monitor and the separate FPS limiter is inactive."
                : "Limits rendering independently of VSync. Useful for lower heat, noise, and power draw." );
            ImText( "Surface Detail", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable Surface Detail", &settings.AllowNormalmaps ) ) {
                Engine::GAPI->UpdateTextureMaxSize();
            }
            ImGui::SameLine();

            static const std::vector<std::pair<const char*, bool>> surfaceDetailModes = {
                {"Normal Maps", false},
                {"Parallax", true},
            };
            ImGui::BeginDisabled( !settings.AllowNormalmaps );
            ImGui::SetNextItemWidth( standardComboWidth );
            if ( ImComboBoxC( "##SurfaceDetailMode", surfaceDetailModes, &settings.EnableParallaxOcclusionMapping, [] {} ) ) {
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Adds normal-map surface detail. Parallax also uses available *_disp.dds height maps; textures without height maps still use normal mapping." );

            static std::vector<std::tuple<const char*, AOMode, const char*>> aoModes = {
                    {"Disabled", AOMode::AO_NONE, nullptr},
                    {"HBAO+", AOMode::AO_HBAO, "NVIDIA HBAO+ (Horizon-Based Ambient Occlusion Plus)"},
                    {"SAO", AOMode::AO_SAO, nullptr},
                    {"ASSAO", AOMode::AO_ASSAO, "Intel ASSAO (Adaptive Screen Space Ambient Occlusion)"},
                    {"XeGTAO", AOMode::AO_XEGTAO, "Intel XeGTAO (Ground Truth Ambient Occlusion)"},
            };
            ImText( "AO Mode", { aoModeLabelWidth, buttonWidth.y } ); ImGui::SameLine();
            ImGui::SetNextItemWidth( compactComboWidth );
            if ( ImComboBoxCT( "##AOMode", aoModes, &settings.AoMode, [] {
                Engine::GraphicsEngine->ReloadShaders( ShaderCategory::Other );
                } ) ) {
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( settings.AoMode == AOMode::AO_NONE );
            ImGui::SetNextItemWidth( standardComboWidth );
            SliderNormalizedUiStrength( "##AOStrength", &settings.AOStrength );
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Controls the overall ambient occlusion strength. Mode-specific fine tuning remains available in Advanced Settings." );
            bool screenSpaceLightFX = settings.EnableContactShadows || settings.EnableScreenSpaceGI;
            ImText( "Screen-Space Light FX", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable Screen-Space Light FX", &screenSpaceLightFX ) ) {
                settings.EnableContactShadows = screenSpaceLightFX;
                settings.EnableScreenSpaceGI = screenSpaceLightFX;
                shadersToReload |= ShaderCategory::Other;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( !screenSpaceLightFX );
            ImGui::SetNextItemWidth( standardComboWidth );
            float screenSpaceLightFXStrength = (settings.ContactShadowStrength + settings.ScreenSpaceGIStrength) * 0.5f;
            if ( SliderNormalizedUiStrength( "##ScreenSpaceLightFXStrength", &screenSpaceLightFXStrength ) ) {
                settings.ContactShadowStrength = screenSpaceLightFXStrength;
                settings.ScreenSpaceGIStrength = screenSpaceLightFXStrength;
                shadersToReload |= ShaderCategory::Other;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Adds small contact shadows and subtle screen-space indirect light. Separate fine tuning remains available in Advanced Settings." );

            ImText( "Godrays", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable Godrays", &settings.EnableGodRays ) ) {
                Engine::GraphicsEngine->ReloadShaders( ShaderCategory::Other );
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( !settings.EnableGodRays );
            ImGui::SetNextItemWidth( standardComboWidth );
            SliderNormalizedUiStrength( "##GodrayStrength", &settings.GodRayStrength );
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Adds sunlight beams when the sun is partially blocked by trees, buildings, or terrain." );
            bool enhancedWater = settings.EnableSSR;
            ImText( "Water Effects", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable Water Effects", &enhancedWater ) ) {
                settings.EnableSSR = enhancedWater;
                settings.EnableWaterAnimation = enhancedWater;
                shadersToReload |= ShaderCategory::Water;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( !enhancedWater );
            ImGui::SetNextItemWidth( standardComboWidth );
            SliderNormalizedUiStrength( "##WaterEffectsStrength", &settings.SSRStrength );
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Enables water reflections, animated water movement, and wet-ground reflection strength." );

            ImText( "Backlit Vegetation", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            ImGui::Checkbox( "##Enable Backlit Vegetation", &settings.EnableSSS );
            ImGui::SameLine();
            ImGui::BeginDisabled( !settings.EnableSSS );
            ImGui::SetNextItemWidth( standardComboWidth );
            float backlitVegetationStrength = settings.SSSIntensity / 0.75f;
            if ( SliderNormalizedUiStrength( "##BacklitVegetationStrength", &backlitVegetationStrength ) ) {
                settings.SSSIntensity = backlitVegetationStrength * 0.75f;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Adds soft backlighting through leaves, grass, and alpha-tested vegetation." );

            ImText( "Depth of Field", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            ImGui::Checkbox( "##Enable Depth of Field", &settings.EnableDoF );
            ImGui::SameLine();
            ImGui::BeginDisabled( !settings.EnableDoF );
            ImGui::SetNextItemWidth( standardComboWidth );
            float depthOfFieldStrength = settings.DoFBokehRadius / 3.5f;
            if ( SliderNormalizedUiStrength( "##DepthOfFieldBlurStrength", &depthOfFieldStrength ) ) {
                settings.DoFBokehRadius = depthOfFieldStrength * 3.5f;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( "Controls camera blur strength. Focus range and near/far blur tuning remain available in Advanced Settings." );

#if defined(BUILD_GOTHIC_2_6_fix) || (defined(BUILD_GOTHIC_1_08k) && !defined(BUILD_1_12F))
#if defined(BUILD_GOTHIC_1_08k) && !defined(BUILD_1_12F)
            if ( haveWindAnimations )
#endif
            {
                bool windEffect = settings.WindQuality != GothicRendererSettings::EWindQuality::WIND_QUALITY_NONE;
                ImText( "Wind Effect", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
                if ( ImGui::Checkbox( "##Enable Wind Effect", &windEffect ) ) {
                    settings.WindQuality = windEffect
                        ? GothicRendererSettings::EWindQuality::WIND_QUALITY_ADVANCED
                        : GothicRendererSettings::EWindQuality::WIND_QUALITY_NONE;
                    shadersToReload |= ShaderCategory::Other;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled( !windEffect );
                ImGui::SetNextItemWidth( standardComboWidth );
                SliderNormalizedUiStrength( "##WindEffectStrength", &settings.GlobalWindStrength );
                ImGui::EndDisabled();
                ImGui::SetItemTooltip( "Controls animated wind movement for trees, grass, and wheat." );
            }

            ImText( "Characters affect objects", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            if ( ImGui::Checkbox( "##Enable Characters affect objects", &settings.HeroAffectsObjects ) ) {
                shadersToReload |= ShaderCategory::Other;
            }
            ImGui::SetItemTooltip( "Lets grass and wheat bend locally around the hero and up to five nearby NPCs." );
#endif //BUILD_GOTHIC_2_6_fix

            ImText( "Enable Rain", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            ImGui::Checkbox( "##Enable Rain", &settings.EnableRain );
            ImGui::SetItemTooltip( "Turns weather particles and wet-ground rain effects on or off." );
            ImText( "Limit Light Intensity", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            ImGui::Checkbox( "##Limit Light Intensity", &settings.LimitLightIntesity );
            ImGui::SetItemTooltip( "Limits overly bright point lights to reduce blown-out interiors." );
            ImText( "Occlusion Culling", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            ImGui::Checkbox( "##Enable Occlusion Culling", &settings.EnableOcclusionCulling );
            ImGui::SetItemTooltip( "Skips hidden world sections for better performance. Conservative visibility checks reduce visible pop-in." );
            ImText( "HDR", { buttonWidth.x - ImGui::GetFrameHeight() - style.ItemSpacing.x, buttonWidth.y } ); ImGui::SameLine();
            ImGui::Checkbox( "##Enable HDR", &settings.EnableHDR );
            ImGui::SetItemTooltip( "Enables high dynamic range rendering for the renderer post-processing pipeline." );
            ImGui::EndGroup();
        }

        ImGui::Spacing();
        static const char* advancedSettingsHint = "Advanced settings: CTRL+F11 ";
        const float advancedHintWidth = ImGui::CalcTextSize( advancedSettingsHint ).x;
        ImGui::SetCursorPosX( std::max( ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - advancedHintWidth ) );
        ImGui::TextUnformatted( advancedSettingsHint );
        
        const float footerButtonWidth = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
        const bool cancelled = ImGui::Button( "Cancel", ImVec2( footerButtonWidth, 30.f ) );
        ImGui::SetItemTooltip( "Discard changes made since opening the F11 menu." );
        ImGui::SameLine();
        const bool saved = ImGui::Button( "Save Settings", ImVec2( footerButtonWidth, 30.f ) );
        auto worldSettingsPath = Engine::GAPI->GetLoadedWorldSettingsPath(false);
        const bool isInWorld = !worldSettingsPath.empty();
        const bool hasWorldSettings = Toolbox::FileExists( worldSettingsPath );
        if ( ( ImGui::GetIO().KeyCtrl || hasWorldSettings ) && isInWorld ) {
            ImGui::SetItemTooltip("Save settings to \"%s\"", worldSettingsPath.c_str());
        } else {
            ImGui::SetItemTooltip("Save settings.\nCTRL+Click to save just for the current world.");
        }
        
        if ( cancelled ) {
            CancelSettingsEdit();
            shadersToReload = ShaderCategory::None;
            Engine::GraphicsEngine->OnUIEvent( BaseGraphicsEngine::UI_ClosedSettings );
        } else if ( saved ) {
            CommitSettingsEdit();
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

        ImGui::Checkbox( "Sort Render Queue", &settings.SortRenderQueue );
        ImGui::Checkbox( "Draw Threaded", &settings.DrawThreaded );
        ImGui::Checkbox( "Do Z Prepass", &settings.DoZPrepass );
        ImGui::SetItemTooltip("Lightweight depth prepass. It can help low-bandwidth systems and hurt others." );

        float largeObjectDrawDistance = settings.OutdoorVobDrawRadius / 1000.0f;
        if ( ImGui::SliderFloat( "Large Object Draw Distance", &largeObjectDrawDistance, 1.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp ) ) {
            settings.OutdoorVobDrawRadius = largeObjectDrawDistance * 1000.0f;
        }
        ImGui::SetItemTooltip( "Controls the draw distance of large static VOBs. This setting is intentionally not changed by F11 graphics presets." );

        float visualFXDrawDistance = settings.VisualFXDrawRadius / 1000.0f;
        if ( ImGui::SliderFloat( "VisualFX Draw Distance", &visualFXDrawDistance, 0.1f, 10.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp ) ) {
            settings.VisualFXDrawRadius = visualFXDrawDistance * 1000.0f;
        }
        ImGui::SetItemTooltip( "Controls the draw distance of particle, flash, and associated dynamic-light effects. This setting is intentionally not changed by F11 graphics presets." );

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
            SliderNormalizedUiStrength( "Wind Strength", &settings.GlobalWindStrength );
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
        } else if ( settings.AoMode == AOMode::AO_XEGTAO ) {
            static std::vector<std::pair<const char*, int>> xegtaoQuality = {
                {"Low", 0}, {"Medium", 1}, {"High", 2}, {"Ultra", 3}
            };
            if ( ImComboBox( "Quality", xegtaoQuality, &settings.XegtaoSettings.QualityLevel ) ) ImGui::EndCombo();
            static std::vector<std::pair<const char*, int>> xegtaoDenoising = {
                {"Sharp", 1}, {"Medium", 2}, {"Soft", 3}
            };
            if ( ImComboBox( "Denoising", xegtaoDenoising, &settings.XegtaoSettings.DenoisePasses ) ) ImGui::EndCombo();
            ImGui::SliderFloat( "Effect Radius", &settings.XegtaoSettings.Radius, 10.0f, 200.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
            ImGui::SetItemTooltip( "Ambient-occlusion radius in Gothic view-space units. 100 units equal approximately one meter." );
        }
        ImGui::PopID();

        ImGui::SeparatorText( "Parallax Occlusion Mapping" );
        ImGui::BeginDisabled( !settings.AllowNormalmaps || !settings.EnableParallaxOcclusionMapping );
        ImGui::SliderFloat( "POM Strength", &settings.ParallaxOcclusionStrength, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Water Effects" );
        ImGui::BeginDisabled( !settings.EnableSSR );
        SliderNormalizedUiStrength( "SSR Strength", &settings.SSRStrength );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Screen-Space Light FX" );
        const bool screenSpaceLightFX = settings.EnableContactShadows || settings.EnableScreenSpaceGI;
        ImGui::BeginDisabled( !screenSpaceLightFX );
        bool reloadScreenSpaceLightFX = false;
        reloadScreenSpaceLightFX |= SliderNormalizedUiStrength( "Contact Shadows", &settings.ContactShadowStrength );
        reloadScreenSpaceLightFX |= SliderNormalizedUiStrength( "Indirect Light", &settings.ScreenSpaceGIStrength );
        if ( reloadScreenSpaceLightFX ) Engine::GraphicsEngine->ReloadShaders( ShaderCategory::Other );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Particles" );
        ImGui::Checkbox( "Adapt to Scene Lighting", &settings.EnableParticleLighting );
        ImGui::BeginDisabled( !settings.EnableParticleLighting );
        SliderNormalizedUiStrength( "Lighting Adaptation", &settings.ParticleLightingStrength );
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Backlit Vegetation" );
        ImGui::BeginDisabled( !settings.EnableSSS );
        float advancedBacklitVegetationStrength = settings.SSSIntensity / 0.75f;
        if ( SliderNormalizedUiStrength( "Intensity", &advancedBacklitVegetationStrength ) ) {
            settings.SSSIntensity = advancedBacklitVegetationStrength * 0.75f;
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText( "Depth of Field" );
        ImGui::BeginDisabled( !settings.EnableDoF );
        ImGui::SliderFloat( "Blur Distance", &settings.DoFFocusDistance, 0.0f, 30000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
        ImGui::SliderFloat( "Focus Range", &settings.DoFFocusRange, 100.0f, 30000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
        float advancedDepthOfFieldStrength = settings.DoFBokehRadius / 3.5f;
        if ( SliderNormalizedUiStrength( "Blur Strength", &advancedDepthOfFieldStrength ) ) {
            settings.DoFBokehRadius = advancedDepthOfFieldStrength * 3.5f;
        }
        ImGui::SliderFloat( "Near Blur Distance", &settings.DoFNearBlurDistance, 0.0f, 1000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
        SliderNearDoFStrength( "Near Blur Strength", &settings.DoFNearBlurStrength );
        ImGui::SetItemTooltip( "Controls near-camera blur up to 5x. The image center stays sharp while the effect increases toward the sides; far-distance blur is unchanged." );
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

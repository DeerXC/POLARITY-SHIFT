#include "core/TextRenderer.h"

#include "core/DirectX.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "gdi32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
struct TextCommand
{
    std::wstring text;

    float x = 0.0f;
    float y = 0.0f;

    TextStyle style{};
};

struct TextFormatKey
{
    int pixelSize = 0;
    bool display = false;
    bool bold = false;

    bool operator<(const TextFormatKey& other) const
    {
        if (pixelSize != other.pixelSize)
        {
            return pixelSize < other.pixelSize;
        }

        if (bold != other.bold)
        {
            return bold < other.bold;
        }

        return display < other.display;
    }
};

ComPtr<ID2D1Factory> g_D2DFactory;

ComPtr<ID2D1RenderTarget> g_D2DRenderTarget;

ComPtr<IDWriteFactory> g_DWriteFactory;

std::wstring g_FamilyOverride; // the CJK family Loc asked for ("PS CJK SC" / "PS CJK JP"): it says which language's CJK fonts to use

ComPtr<ID2D1SolidColorBrush> g_TextBrush;

ComPtr<ID2D1SolidColorBrush> g_ShadowBrush;

std::map<TextFormatKey, ComPtr<IDWriteTextFormat>> g_TextFormatCache;

std::vector<TextCommand> g_TextCommands;

constexpr float FONT_SIZE_SCALE = 3.0f;

constexpr float MIN_FONT_SIZE = 10.0f;

constexpr float MAX_FONT_SIZE = 360.0f;

constexpr float SHADOW_OFFSET_RATE = 0.065f;
float g_GlobalAlpha = 1.0f;

// Text layouts are cached: nearly every text of a frame is the same as in the frame before, and creating a layout (with the CJK run
// formatting) is the most expensive part of drawing a line of text.
// Two generations: when the current one is full it becomes the old one and a new one begins; a layout that is still in use is found in the
// old generation and moved over. (Clearing one full map would make every text of the next frame be laid out again: a visible hitch.)
std::unordered_map<std::wstring, ComPtr<IDWriteTextLayout>> g_LayoutCache;
std::unordered_map<std::wstring, ComPtr<IDWriteTextLayout>> g_LayoutCacheOld;
constexpr size_t LAYOUT_CACHE_MAX = 400;

std::wstring GetExecutableDirectory()
{
    wchar_t executablePath[MAX_PATH]{};

    const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);

    if (pathLength == 0 || pathLength >= MAX_PATH)
    {
        return {};
    }

    std::wstring directory = executablePath;

    const size_t slashPosition = directory.find_last_of(L"\\/");

    if (slashPosition == std::wstring::npos)
    {
        return {};
    }

    directory.resize(slashPosition);

    return directory;
}

// ---- fonts ------------------------------------------------------------------------------------------------------------------------------
// Every font of the game is in assets/fonts and is shipped with it (SIL OFL). Two layers, one look in three languages:
//   Latin + digits  Chakra Petch       Medium (500) for text, Bold (700) for heavy text
//   CJK body        Noto Sans SC / M PLUS 1   Medium / Bold    (zh / ja)
//   CJK display     Smiley Sans / Dela Gothic One               (zh / ja: heavy text at title size - headers, cards, cut-ins)
// The files are put into a private DirectWrite font collection (IDWriteFactory5 font set: nothing is installed on the system). On an old
// Windows without it they are registered with GDI (FR_PRIVATE) and found in the system collection, as before. Every family has fallbacks,
// down to the fonts of Windows itself, so a missing file never leaves the text blank.
ComPtr<IDWriteFontCollection> g_FontCollection; // the private collection (null: the system collection is used)
std::vector<std::wstring> g_GdiFontPaths;        // files registered with GDI (fallback path), removed in Finalize
std::map<int, std::pair<float, float>> g_LineMetricsCache;

// the families in use (resolved once the fonts are loaded)
std::wstring g_LatinFamily = L"Segoe UI";
bool g_LatinIsKenney = false; // the old display font: its X looks like an H, that letter is taken from Segoe UI
std::wstring g_BodyCjk[2];    // 0 zh, 1 ja
std::wstring g_DisplayCjk[2];
float g_DisplayCjkScale[2] = {1.0f, 1.0f};
bool g_DisplayCjkFound[2] = {false, false};

constexpr float CJK_SIZE_SCALE = 0.94f;     // CJK glyphs fill the em more than Latin capitals: a little smaller looks the same size
constexpr float DISPLAY_MIN_SIZE = 0.0111f; // heavy text from this size (about 24 px of 1280 x 720) uses the display layer
// is this text display text? layer: -1 the old rule, 0 body, 1 display
inline bool IsDisplay(int layer, bool bold, float size)
{
    return layer < 0 ? (bold && size >= DISPLAY_MIN_SIZE) : layer > 0;
}

int CurrentCjk()
{
    return (g_FamilyOverride.find(L"JP") != std::wstring::npos) ? 1 : 0; // (Loc asks for "PS CJK JP" / "PS CJK SC")
}

IDWriteFontCollection* CollectionFor(const std::wstring& family)
{
    if (g_FontCollection != nullptr)
    {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(g_FontCollection->FindFamilyName(family.c_str(), &index, &exists)) && exists) return g_FontCollection.Get();
    }
    return nullptr; // (the system collection)
}

bool FamilyExists(const wchar_t* family)
{
    if (CollectionFor(family) != nullptr) return true;
    ComPtr<IDWriteFontCollection> system;
    if (g_DWriteFactory == nullptr || FAILED(g_DWriteFactory->GetSystemFontCollection(system.GetAddressOf(), TRUE))) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(system->FindFamilyName(family, &index, &exists)) && exists;
}

std::wstring FirstFamily(std::initializer_list<const wchar_t*> names)
{
    for (const wchar_t* name : names)
        if (FamilyExists(name)) return name;
    return *(names.end() - 1);
}

void LoadFonts()
{
    g_FontCollection.Reset();
    g_GdiFontPaths.clear();
    const std::wstring directory = GetExecutableDirectory() + L"\\assets\\fonts\\";
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW found{};
    for (const wchar_t* pattern : {L"*.ttf", L"*.otf"})
    {
        HANDLE handle = FindFirstFileW((directory + pattern).c_str(), &found);
        if (handle == INVALID_HANDLE_VALUE) continue;
        do
        {
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) files.push_back(directory + found.cFileName);
        } while (FindNextFileW(handle, &found));
        FindClose(handle);
    }

    // the private collection (Windows 10 1703 and later)
    ComPtr<IDWriteFactory5> factory5;
    if (!files.empty() && SUCCEEDED(g_DWriteFactory.As(&factory5)))
    {
        ComPtr<IDWriteFontSetBuilder1> builder;
        if (SUCCEEDED(factory5->CreateFontSetBuilder(builder.GetAddressOf())))
        {
            for (const std::wstring& file : files)
            {
                ComPtr<IDWriteFontFile> fontFile;
                if (SUCCEEDED(factory5->CreateFontFileReference(file.c_str(), nullptr, fontFile.GetAddressOf()))) builder->AddFontFile(fontFile.Get());
            }
            ComPtr<IDWriteFontSet> set;
            ComPtr<IDWriteFontCollection1> collection;
            if (SUCCEEDED(builder->CreateFontSet(set.GetAddressOf())) && SUCCEEDED(factory5->CreateFontCollectionFromFontSet(set.Get(), collection.GetAddressOf())))
                g_FontCollection = collection;
        }
    }
    if (g_FontCollection == nullptr)
    {
        for (const std::wstring& file : files)
            if (AddFontResourceExW(file.c_str(), FR_PRIVATE, nullptr) > 0) g_GdiFontPaths.push_back(file);
        if (!g_GdiFontPaths.empty()) SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 1000, nullptr);
    }

    g_LatinFamily = FirstFamily({L"Chakra Petch", L"Kenney Future", L"Segoe UI"});
    g_LatinIsKenney = g_LatinFamily == L"Kenney Future";
    g_BodyCjk[0] = FirstFamily({L"Noto Sans SC", L"PS CJK SC", L"Microsoft YaHei UI"});
    g_BodyCjk[1] = FirstFamily({L"M PLUS 1", L"Noto Sans JP", L"PS CJK JP", L"Yu Gothic UI"});
    g_DisplayCjkFound[0] = FamilyExists(L"Smiley Sans") || FamilyExists(L"\u5F97\u610F\u9ED1");
    g_DisplayCjk[0] = g_DisplayCjkFound[0] ? FirstFamily({L"Smiley Sans", L"\u5F97\u610F\u9ED1"}) : g_BodyCjk[0];
    g_DisplayCjkScale[0] = g_DisplayCjkFound[0] ? 1.02f : CJK_SIZE_SCALE; // (Smiley Sans is narrow and short: it can be as large as the Latin)
    g_DisplayCjkFound[1] = FamilyExists(L"Dela Gothic One");
    g_DisplayCjk[1] = g_DisplayCjkFound[1] ? L"Dela Gothic One" : g_BodyCjk[1];
    g_DisplayCjkScale[1] = g_DisplayCjkFound[1] ? 0.90f : CJK_SIZE_SCALE; // (Dela Gothic One is very wide and heavy)

    g_TextFormatCache.clear();
    g_LayoutCache.clear();
    g_LayoutCacheOld.clear();
    g_LineMetricsCache.clear();
}

void UnloadFonts()
{
    for (const std::wstring& path : g_GdiFontPaths) RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    g_GdiFontPaths.clear();
    g_FontCollection.Reset();
}

bool IsCjkCharacter(wchar_t c)
{
    return c == 0x00B7 || (c >= 0x2014 && c <= 0x2015) || (c >= 0x2018 && c <= 0x201F) || c == 0x2026
           || (c >= 0x2E80 && c <= 0x2FDF) || (c >= 0x3000 && c <= 0x30FF) || (c >= 0x3100 && c <= 0x31FF)
           || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xFE30 && c <= 0xFE4F)
           || (c >= 0xFF00 && c <= 0xFFEF);
}

bool IsKenneyX(wchar_t c)
{
    return g_LatinIsKenney && (c == L'X' || c == L'x');
}

// Puts a string that contains CJK characters on the same baseline / line height as the Latin font, then switches the CJK runs to the CJK
// family of the language and the layer (display: heavy text at title size). Strings without CJK characters are left untouched.
void ApplyCjkFormatting(IDWriteTextLayout* layout, IDWriteTextFormat* baseFormat, const std::wstring& text, float fontSize, int metricsKey, bool bold,
                        bool display)
{
    if (layout == nullptr || baseFormat == nullptr || g_DWriteFactory == nullptr)
    {
        return;
    }

    bool hasCjk = false;
    bool hasX = false;
    for (const wchar_t c : text)
    {
        if (IsCjkCharacter(c)) hasCjk = true;
        else if (IsKenneyX(c)) hasX = true;
    }
    if (!hasCjk && !hasX)
    {
        return;
    }

    auto cached = g_LineMetricsCache.find(metricsKey);
    if (cached == g_LineMetricsCache.end())
    {
        ComPtr<IDWriteTextLayout> reference;
        if (SUCCEEDED(g_DWriteFactory->CreateTextLayout(L"H", 1, baseFormat, 1000.0f, 1000.0f, reference.GetAddressOf())))
        {
            DWRITE_LINE_METRICS lineMetrics{};
            UINT32 lineCount = 0;
            if (SUCCEEDED(reference->GetLineMetrics(&lineMetrics, 1, &lineCount)) && lineCount > 0 && lineMetrics.height > 0.0f)
                cached = g_LineMetricsCache.emplace(metricsKey, std::make_pair(lineMetrics.height, lineMetrics.baseline)).first;
        }
    }
    if (cached != g_LineMetricsCache.end())
    {
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, cached->second.first, cached->second.second);
    }

    const int lang = CurrentCjk();
    const bool useDisplay = display && g_DisplayCjkFound[lang];
    const std::wstring& family = useDisplay ? g_DisplayCjk[lang] : g_BodyCjk[lang];
    IDWriteFontCollection* collection = CollectionFor(family);
    const DWRITE_FONT_WEIGHT weight = useDisplay ? DWRITE_FONT_WEIGHT_NORMAL : (bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_MEDIUM);
    const float scale = useDisplay ? g_DisplayCjkScale[lang] : CJK_SIZE_SCALE;
    const wchar_t* locale = lang == 1 ? L"ja-jp" : L"zh-cn";

    const UINT32 length = static_cast<UINT32>(text.length());
    UINT32 index = 0;
    while (index < length)
    {
        if (!IsCjkCharacter(text[index]))
        {
            index++;
            continue;
        }
        const UINT32 start = index;
        while (index < length && IsCjkCharacter(text[index])) index++;
        DWRITE_TEXT_RANGE range{start, index - start};
        layout->SetFontCollection(collection, range);
        layout->SetFontFamilyName(family.c_str(), range);
        layout->SetFontWeight(weight, range);
        layout->SetFontStyle(DWRITE_FONT_STYLE_NORMAL, range); // (Smiley Sans is an oblique face: the closest match is taken)
        layout->SetFontSize(fontSize * scale, range);
        layout->SetLocaleName(locale, range);
    }

    if (hasX)
    {
        for (UINT32 i = 0; i < length; i++)
        {
            if (!IsKenneyX(text[i])) continue;
            DWRITE_TEXT_RANGE range{i, 1};
            layout->SetFontCollection(nullptr, range);
            layout->SetFontFamilyName(L"Segoe UI", range);
            layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            layout->SetFontSize(fontSize * 0.92f, range);
            layout->SetLocaleName(L"en-us", range);
        }
    }
}

std::wstring ConvertToWideString(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }

    const int requiredLength = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), nullptr, 0);

    if (requiredLength <= 0)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring result(static_cast<size_t>(requiredLength), L'\0');

    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), result.data(), requiredLength);

    return result;
}

void ShowTextRendererError(const char* message, HRESULT result)
{
    char errorMessage[512]{};

    wsprintfA(errorMessage, "%s\n\nHRESULT: 0x%08X", message, static_cast<unsigned int>(result));

    MessageBoxA(nullptr, errorMessage, "Text Renderer Error", MB_OK | MB_ICONERROR);
}

// (the text is placed in the 16:9 game rectangle of the back buffer, see DirectXSystem::GetGameRect)
float ToPixelX(float normalizedX)
{
    int gx, gy, gw, gh;
    DirectXSystem::GetGameRect(gx, gy, gw, gh);
    return static_cast<float>(gx) + (normalizedX + 1.0f) * 0.5f * static_cast<float>(gw);
}

float ToPixelY(float normalizedY)
{
    int gx, gy, gw, gh;
    DirectXSystem::GetGameRect(gx, gy, gw, gh);
    return static_cast<float>(gy) + (1.0f - normalizedY) * 0.5f * static_cast<float>(gh);
}

float ToPixelWidth(float normalizedWidth)
{
    int gx, gy, gw, gh;
    DirectXSystem::GetGameRect(gx, gy, gw, gh);
    return normalizedWidth * 0.5f * static_cast<float>(gw);
}

float CalculateFontSize(float normalizedSize)
{
    int gx, gy, gw, gh;
    DirectXSystem::GetGameRect(gx, gy, gw, gh);
    const float height = static_cast<float>(gh);

    const float fontSize = normalizedSize * FONT_SIZE_SCALE * height;

    return std::clamp(fontSize, MIN_FONT_SIZE, MAX_FONT_SIZE);
}

bool CreateRenderTargetResources()
{
    if (g_D2DRenderTarget == nullptr)
    {
        return false;
    }

    HRESULT result = g_D2DRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), g_TextBrush.GetAddressOf());

    if (FAILED(result))
    {
        ShowTextRendererError("Creating text brush failed.", result);

        return false;
    }

    result = g_D2DRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.005f, 0.012f, 0.035f, 0.72f), g_ShadowBrush.GetAddressOf());

    if (FAILED(result))
    {
        ShowTextRendererError("Creating shadow brush failed.", result);

        return false;
    }

    return true;
}

bool CreateRenderTarget()
{
    g_TextBrush.Reset();
    g_ShadowBrush.Reset();
    g_D2DRenderTarget.Reset();

    IDXGISwapChain* swapChain = DirectXSystem::GetSwapChain();

    if (swapChain == nullptr || g_D2DFactory == nullptr)
    {
        return false;
    }

    ComPtr<IDXGISurface> backBufferSurface;

    const HRESULT surfaceResult = swapChain->GetBuffer(0, IID_PPV_ARGS(backBufferSurface.GetAddressOf()));

    if (FAILED(surfaceResult))
    {
        ShowTextRendererError("Getting DXGI surface failed.", surfaceResult);

        return false;
    }

    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f,
        D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);

    const HRESULT targetResult =
        g_D2DFactory->CreateDxgiSurfaceRenderTarget(backBufferSurface.Get(), &properties, g_D2DRenderTarget.GetAddressOf());

    if (FAILED(targetResult))
    {
        ShowTextRendererError("Creating Direct2D target failed.", targetResult);

        return false;
    }

    g_D2DRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

    g_D2DRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    return CreateRenderTargetResources();
}

IDWriteTextFormat* GetTextFormat(float normalizedSize, bool bold)
// (the Latin family is the same for both layers: Chakra Petch Bold carries the titles too)
{
    if (g_DWriteFactory == nullptr)
    {
        return nullptr;
    }

    const float fontSize = CalculateFontSize(normalizedSize);

    const int pixelSize = static_cast<int>(std::round(fontSize * 10.0f));

    const TextFormatKey key{pixelSize, bold, false};

    const auto existing = g_TextFormatCache.find(key);

    if (existing != g_TextFormatCache.end())
    {
        return existing->second.Get();
    }

    ComPtr<IDWriteTextFormat> textFormat;

    const wchar_t* fontFamily = g_LatinFamily.c_str();

    DWRITE_FONT_WEIGHT fontWeight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_MEDIUM;

    if (g_LatinIsKenney)
    {
        fontWeight = DWRITE_FONT_WEIGHT_NORMAL; // (Kenney Future has one weight)
    }

    HRESULT result = g_DWriteFactory->CreateTextFormat(fontFamily, CollectionFor(g_LatinFamily), fontWeight, DWRITE_FONT_STYLE_NORMAL,
                                                       DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-us", textFormat.GetAddressOf());

    if (FAILED(result) || textFormat == nullptr)
    {
        textFormat.Reset();

        result = g_DWriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-us", textFormat.GetAddressOf());
    }

    if (FAILED(result) || textFormat == nullptr)
    {
        ShowTextRendererError("CreateTextFormat failed.", result);

        return nullptr;
    }

    textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    IDWriteTextFormat* resultFormat = textFormat.Get();

    g_TextFormatCache.emplace(key, std::move(textFormat));

    return resultFormat;
}

DWRITE_TEXT_ALIGNMENT
ConvertAlignment(TextAlignment alignment)
{
    switch (alignment)
    {
    case TextAlignment::Center:
        return DWRITE_TEXT_ALIGNMENT_CENTER;

    case TextAlignment::Right:
        return DWRITE_TEXT_ALIGNMENT_TRAILING;

    case TextAlignment::Left:
    default:
        return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

IDWriteTextLayout* GetTextLayout(const TextCommand& command, IDWriteTextFormat* textFormat, float fontSize, float layoutWidth,
                                 float layoutHeight)
{
    std::wstring key = command.text;
    key += L'\x1F';
    key += std::to_wstring(static_cast<int>(std::round(fontSize * 10.0f)));
    key += L'|';
    key += std::to_wstring(static_cast<int>(std::round(layoutWidth)));
    key += L'|';
    key += std::to_wstring(static_cast<int>(command.style.alignment));
    key += IsDisplay(command.style.layer, command.style.bold, command.style.size) ? L'D' : (command.style.bold ? L'B' : L'N');

    const auto found = g_LayoutCache.find(key);
    if (found != g_LayoutCache.end())
    {
        return found->second.Get();
    }

    if (g_LayoutCache.size() >= LAYOUT_CACHE_MAX)
    {
        g_LayoutCacheOld.swap(g_LayoutCache);
        g_LayoutCache.clear();
    }

    const auto old = g_LayoutCacheOld.find(key);
    if (old != g_LayoutCacheOld.end())
    {
        ComPtr<IDWriteTextLayout> moved = std::move(old->second);
        g_LayoutCacheOld.erase(old);
        return g_LayoutCache.emplace(std::move(key), std::move(moved)).first->second.Get();
    }

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_DWriteFactory->CreateTextLayout(command.text.c_str(), static_cast<UINT32>(command.text.length()), textFormat,
                                                 layoutWidth, layoutHeight, layout.GetAddressOf())))
    {
        return nullptr;
    }

    layout->SetTextAlignment(ConvertAlignment(command.style.alignment));
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    ApplyCjkFormatting(layout.Get(), textFormat, command.text, fontSize, static_cast<int>(std::round(fontSize * 10.0f)) * 2 + (command.style.bold ? 1 : 0),
                       command.style.bold, IsDisplay(command.style.layer, command.style.bold, command.style.size));

    return g_LayoutCache.emplace(std::move(key), std::move(layout)).first->second.Get();
}

void DrawCommand(const TextCommand& command)
{
    if (command.text.empty() || g_D2DRenderTarget == nullptr || g_DWriteFactory == nullptr || g_TextBrush == nullptr
        || g_ShadowBrush == nullptr)
    {
        return;
    }

    IDWriteTextFormat* textFormat = GetTextFormat(command.style.size, command.style.bold);

    if (textFormat == nullptr)
    {
        return;
    }

    const int backBufferHeight = DirectXSystem::GetBackBufferHeight();

    if (backBufferHeight <= 0)
    {
        return;
    }

    const float fontSize = CalculateFontSize(command.style.size);

    float layoutWidth = ToPixelWidth(command.style.layoutWidth);

    if (layoutWidth < 1.0f)
    {
        layoutWidth = 1.0f;
    }

    const float layoutHeight = fontSize * 1.60f;

    IDWriteTextLayout* textLayout = GetTextLayout(command, textFormat, fontSize, layoutWidth, layoutHeight);

    if (textLayout == nullptr)
    {
        return;
    }

    const float pixelX = ToPixelX(command.x);

    const float pixelY = ToPixelY(command.y) - fontSize * 0.18f;

    if (command.style.shadow)
    {
        const float shadowOffset = fontSize * SHADOW_OFFSET_RATE;

        g_ShadowBrush->SetColor(D2D1::ColorF(0.005f, 0.012f, 0.035f, command.style.alpha * 0.72f));

        g_D2DRenderTarget->DrawTextLayout(D2D1::Point2F(pixelX + shadowOffset, pixelY + shadowOffset), textLayout,
                                          g_ShadowBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    g_TextBrush->SetColor(D2D1::ColorF(command.style.red, command.style.green, command.style.blue, command.style.alpha));

    g_D2DRenderTarget->DrawTextLayout(D2D1::Point2F(pixelX, pixelY), textLayout, g_TextBrush.Get(),
                                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
}

bool TextRenderer::Initialize()
{
    g_TextCommands.clear();
    g_TextFormatCache.clear();
    g_LayoutCache.clear();
    g_LayoutCacheOld.clear();

    const HRESULT d2dResult = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_D2DFactory.GetAddressOf());

    if (FAILED(d2dResult))
    {
        ShowTextRendererError("D2D1CreateFactory failed.", d2dResult);

        return false;
    }

    const HRESULT dwriteResult = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                                     reinterpret_cast<IUnknown**>(g_DWriteFactory.GetAddressOf()));

    if (FAILED(dwriteResult))
    {
        ShowTextRendererError("DWriteCreateFactory failed.", dwriteResult);

        return false;
    }

    LoadFonts();

    if (!CreateRenderTarget())
    {
        return false;
    }

    return true;
}

void TextRenderer::Finalize()
{
    g_TextCommands.clear();
    g_TextFormatCache.clear();
    g_LayoutCache.clear();
    g_LayoutCacheOld.clear();

    g_TextBrush.Reset();
    g_ShadowBrush.Reset();
    g_D2DRenderTarget.Reset();

    UnloadFonts();
    g_DWriteFactory.Reset();
    g_D2DFactory.Reset();
}

void TextRenderer::BeginFrame()
{
    g_TextCommands.clear();
}

void TextRenderer::QueueText(const std::string& text, float x, float y, const TextStyle& style)
{
    if (text.empty() || style.alpha <= 0.0f || style.size <= 0.0f)
    {
        return;
    }

    TextCommand command{};

    command.text = ConvertToWideString(text);

    command.x = x;
    command.y = y;
    command.style = style;
    command.style.alpha *= g_GlobalAlpha;

    g_TextCommands.push_back(std::move(command));
}

void TextRenderer::QueueText(const std::string& text, float x, float y, float size, float red, float green, float blue,
                             float alpha)
{
    TextStyle style{};

    style.size = size;

    style.red = red;
    style.green = green;
    style.blue = blue;
    style.alpha = alpha;

    style.bold = false;
    style.shadow = true;

    style.alignment = TextAlignment::Left;

    style.layoutWidth = 1.0f;

    QueueText(text, x, y, style);
}

void TextRenderer::Render()
{
    if (g_TextCommands.empty())
    {
        return;
    }

    if (g_D2DRenderTarget == nullptr)
    {
        if (!CreateRenderTarget())
        {
            g_TextCommands.clear();
            return;
        }
    }

    ID3D11DeviceContext* context = DirectXSystem::GetContext();

    if (context != nullptr)
    {
        context->Flush();
    }

    g_D2DRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

    g_D2DRenderTarget->BeginDraw();

    for (const TextCommand& command : g_TextCommands)
    {
        DrawCommand(command);
    }

    const HRESULT drawResult = g_D2DRenderTarget->EndDraw();

    if (drawResult == D2DERR_RECREATE_TARGET)
    {
        ReleaseRenderTarget();
    }
    else if (FAILED(drawResult))
    {
        ShowTextRendererError("Direct2D EndDraw failed.", drawResult);

        ReleaseRenderTarget();
    }

    g_TextCommands.clear();
}

void TextRenderer::ReleaseRenderTarget()
{
    g_LayoutCache.clear();
    g_LayoutCacheOld.clear();
    g_TextCommands.clear();

    g_TextBrush.Reset();
    g_ShadowBrush.Reset();
    g_D2DRenderTarget.Reset();
}

float TextRenderer::MeasureWidth(const std::string& text, float size, bool bold, int layer)
{
    if (text.empty()) return 0.0f;
    if (g_DWriteFactory == nullptr) return -1.0f;
    static std::unordered_map<std::string, float> s_Cache;
    static std::wstring s_CacheLanguage;
    if (s_CacheLanguage != g_FamilyOverride || s_Cache.size() > 3000)
    {
        s_Cache.clear();
        s_CacheLanguage = g_FamilyOverride;
    }
    const float fontSize = CalculateFontSize(size);
    std::string key = text;
    key += '';
    key += std::to_string(static_cast<int>(std::round(fontSize * 10.0f)));
    key += IsDisplay(layer, bold, size) ? 'D' : (bold ? 'B' : 'N');
    const auto found = s_Cache.find(key);
    if (found != s_Cache.end()) return found->second;

    IDWriteTextFormat* format = GetTextFormat(size, bold);
    if (format == nullptr) return -1.0f;
    const std::wstring wide = ConvertToWideString(text);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_DWriteFactory->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.length()), format, 100000.0f, 10000.0f, layout.GetAddressOf())))
        return -1.0f;
    ApplyCjkFormatting(layout.Get(), format, wide, fontSize, static_cast<int>(std::round(fontSize * 10.0f)) * 2 + (bold ? 1 : 0), bold,
                       IsDisplay(layer, bold, size));
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return -1.0f;
    int gx, gy, gw, gh;
    DirectXSystem::GetGameRect(gx, gy, gw, gh);
    const float normalized = gw > 0 ? metrics.widthIncludingTrailingWhitespace * 2.0f / static_cast<float>(gw) : -1.0f;
    s_Cache.emplace(std::move(key), normalized);
    return normalized;
}

void TextRenderer::SetFontFamilyOverride(const wchar_t* family)
{
    g_FamilyOverride = family != nullptr ? family : L"";

    g_TextFormatCache.clear();
    g_LayoutCache.clear();
    g_LayoutCacheOld.clear();
}

 void TextRenderer::SetGlobalAlpha(float alpha)
{
    g_GlobalAlpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}



